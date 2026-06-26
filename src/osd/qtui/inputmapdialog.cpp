// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  inputmapdialog.cpp - in-game input remapping dialog for the qtui OSD
//
//============================================================

#include "inputmapdialog.h"

#include "embedsession.h"
#include "qtinput.h"

#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QVBoxLayout>

#include <map>

namespace osd::qtui {

namespace {
constexpr int kIndexRole = Qt::UserRole + 1;   // EmbedInputMap index on a leaf item
}


InputMapDialog::InputMapDialog(EmbedSession *session, QWidget *parent) :
	QDialog(parent),
	m_session(session)
{
	setWindowTitle(tr("Input Mapping"));
	resize(560, 620);

	auto *const lay = new QVBoxLayout(this);

	// "press a control" capture banner (hidden until capturing)
	m_banner = new QLabel(this);
	m_banner->setWordWrap(true);
	m_banner->setAlignment(Qt::AlignCenter);
	m_banner->setStyleSheet(QStringLiteral(
			"QLabel { background: palette(highlight); color: palette(highlighted-text);"
			" padding: 8px; border-radius: 4px; font-weight: bold; }"));
	m_banner->setVisible(false);
	lay->addWidget(m_banner);

	m_tree = new QTreeWidget(this);
	m_tree->setColumnCount(2);
	m_tree->setHeaderLabels({ tr("Input"), tr("Assigned to") });
	m_tree->setRootIsDecorated(true);
	m_tree->setUniformRowHeights(true);
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_tree->header()->setStretchLastSection(true);
	lay->addWidget(m_tree, 1);

	connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] { updateButtons(); });
	connect(m_tree, &QTreeWidget::itemActivated, this, [this] (QTreeWidgetItem *, int) {
		int const idx = selectedIndex();
		if (idx >= 0)
			startCapture(idx, false);
	});

	auto *const btnRow = new QHBoxLayout;
	m_remapBtn   = new QPushButton(tr("&Set…"), this);
	m_addBtn     = new QPushButton(tr("&Add…"), this);
	m_defaultBtn = new QPushButton(tr("&Default"), this);
	m_clearBtn   = new QPushButton(tr("&Clear"), this);
	m_remapBtn->setToolTip(tr("Replace this input's binding — then press a key or controller button"));
	m_addBtn->setToolTip(tr("Add another binding to this input (OR) — then press a key or controller button"));
	m_defaultBtn->setToolTip(tr("Restore this input to its default binding"));
	m_clearBtn->setToolTip(tr("Clear this input (unbind it)"));
	for (QPushButton *b : { m_remapBtn, m_addBtn, m_defaultBtn, m_clearBtn })
		btnRow->addWidget(b);
	btnRow->addStretch();
	lay->addLayout(btnRow);

	connect(m_remapBtn,   &QPushButton::clicked, this, [this] { int i = selectedIndex(); if (i >= 0) startCapture(i, false); });
	connect(m_addBtn,     &QPushButton::clicked, this, [this] { int i = selectedIndex(); if (i >= 0) startCapture(i, true); });
	connect(m_defaultBtn, &QPushButton::clicked, this, [this] {
		int const i = selectedIndex();
		if (i >= 0 && m_session)
			m_session->post({ EmbedCommand::InputSetDefault, 0.0, i, {} });
	});
	connect(m_clearBtn,   &QPushButton::clicked, this, [this] {
		int const i = selectedIndex();
		if (i >= 0 && m_session)
			m_session->post({ EmbedCommand::InputSetNone, 0.0, i, {} });
	});

	auto *const buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	lay->addWidget(buttons);

	// poll the emulation thread: capture progress + input-map changes
	m_timer = new QTimer(this);
	m_timer->setInterval(60);
	connect(m_timer, &QTimer::timeout, this, &InputMapDialog::tick);
	m_timer->start();

	rebuild();
	updateButtons();
}


InputMapDialog::~InputMapDialog()
{
	if (m_forwarding)
		qApp->removeEventFilter(this);
}


void InputMapDialog::rebuild()
{
	if (!m_session)
		return;

	std::vector<EmbedInputEntry> const entries = m_session->inputMapSnapshot();
	m_lastGen = m_session->inputMapGeneration();

	// remember selection + expansion so a refresh doesn't jump the view
	int const sel = selectedIndex();

	m_tree->clear();
	std::map<QString, QTreeWidgetItem *> groups;
	QTreeWidgetItem *reselect = nullptr;

	for (int i = 0; i < int(entries.size()); ++i)
	{
		EmbedInputEntry const &e = entries[i];
		QString const groupName = QString::fromStdString(e.group);
		auto git = groups.find(groupName);
		if (git == groups.end())
		{
			auto *const g = new QTreeWidgetItem(m_tree, { groupName });
			g->setFirstColumnSpanned(true);
			g->setFlags(g->flags() & ~Qt::ItemIsSelectable);
			git = groups.emplace(groupName, g).first;
		}

		QString binding = QString::fromStdString(e.seqText);
		if (e.isNone || binding.isEmpty())
			binding = tr("(not set)");

		auto *const item = new QTreeWidgetItem(git->second,
				{ QString::fromStdString(e.name), binding });
		item->setData(0, kIndexRole, i);
		if (i == sel)
			reselect = item;
	}

	m_tree->expandAll();
	if (reselect)
	{
		m_tree->setCurrentItem(reselect);
		m_tree->scrollToItem(reselect);
	}
	updateButtons();
}


int InputMapDialog::selectedIndex() const
{
	QTreeWidgetItem *const item = m_tree->currentItem();
	if (!item)
		return -1;
	QVariant const v = item->data(0, kIndexRole);
	return v.isValid() ? v.toInt() : -1;
}


void InputMapDialog::updateButtons()
{
	bool const haveSel = (selectedIndex() >= 0) && (m_capturing < 0);
	m_remapBtn->setEnabled(haveSel);
	m_addBtn->setEnabled(haveSel);
	m_defaultBtn->setEnabled(haveSel);
	m_clearBtn->setEnabled(haveSel);
	m_tree->setEnabled(m_capturing < 0);
}


void InputMapDialog::startCapture(int index, bool append)
{
	if (!m_session || m_capturing >= 0)
		return;
	m_capturing = index;

	// forward keyboard/mouse to the input bus while capturing (joystick input is
	// captured directly by the SDL module regardless of focus)
	if (!m_forwarding)
	{
		qApp->installEventFilter(this);
		m_forwarding = true;
	}

	m_banner->setText(tr("Press a key or controller button…   (Esc to cancel)"));
	m_banner->setVisible(true);
	updateButtons();

	EmbedAction act;
	act.cmd = EmbedCommand::InputCaptureStart;
	act.ival = index;
	act.value = append ? 1u : 0u;   // append (OR) vs replace
	m_session->post(act);
}


void InputMapDialog::endCapture()
{
	if (m_forwarding)
	{
		qApp->removeEventFilter(this);
		m_forwarding = false;
	}
	m_capturing = -1;
	m_banner->setVisible(false);
	rebuild();
}


void InputMapDialog::cancelCapture()
{
	if (m_capturing < 0)
		return;
	if (m_session)
		m_session->post({ EmbedCommand::InputCaptureCancel, 0.0, 0, {} });
	endCapture();
}


void InputMapDialog::tick()
{
	if (!m_session)
		return;

	if (m_capturing >= 0)
	{
		EmbedCapture const c = m_session->captureSnapshot();
		if (c.finished || c.cancelled || !c.active)
		{
			endCapture();   // applies (worker side) + refreshes the bindings
			return;
		}
		QString const partial = QString::fromStdString(c.prompt);
		m_banner->setText(partial.isEmpty()
				? tr("Press a key or controller button…   (Esc to cancel)")
				: tr("%1   (Esc to cancel)").arg(partial));
		return;
	}

	// not capturing: refresh if the input map changed (e.g. via MAME's own UI)
	if (m_session->inputMapGeneration() != m_lastGen)
		rebuild();
}


bool InputMapDialog::eventFilter(QObject *watched, QEvent *event)
{
	if (m_capturing < 0)
		return QDialog::eventFilter(watched, event);

	switch (event->type())
	{
	case QEvent::KeyPress:
	case QEvent::KeyRelease:
	{
		auto *const ke = static_cast<QKeyEvent *>(event);
		if (event->type() == QEvent::KeyPress && ke->key() == Qt::Key_Escape)
		{
			cancelCapture();
			return true;
		}
		if (!ke->isAutoRepeat())
		{
			QtInputEvent e;
			e.type = (event->type() == QEvent::KeyPress) ? QtInputType::KeyPress : QtInputType::KeyRelease;
			e.key = ke->key();
			e.nativeScanCode = ke->nativeScanCode();
			e.modifiers = unsigned(ke->modifiers());
			QtInputBus::instance().pushKeyboard(e);
		}
		return true;   // don't let the captured key drive the dialog/UI
	}
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonRelease:
	{
		auto *const me = static_cast<QMouseEvent *>(event);
		int idx = -1;
		switch (me->button())
		{
		case Qt::LeftButton:   idx = 0; break;
		case Qt::RightButton:  idx = 1; break;
		case Qt::MiddleButton: idx = 2; break;
		default: break;
		}
		if (idx >= 0)
		{
			QtInputEvent e;
			e.type = QtInputType::MouseButton;
			e.button = idx;
			e.value = (event->type() == QEvent::MouseButtonPress) ? 1 : 0;
			QtInputBus::instance().pushMouse(e);
		}
		return true;
	}
	default:
		break;
	}
	return QDialog::eventFilter(watched, event);
}

} // namespace osd::qtui

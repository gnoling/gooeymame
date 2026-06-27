// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  pluginmenudialog.cpp - in-game Plugin Options menu for the qtui OSD
//
//============================================================

#include "pluginmenudialog.h"

#include "embedsession.h"
#include "qtinput.h"   // QtInputBus: forward key/mouse to the machine while a plugin polls

#include <QtCore/QTimer>
#include <QtGui/QCloseEvent>
#include <QtGui/QFont>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPalette>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include <string>

namespace osd::qtui {

namespace {
constexpr int kNameRole = Qt::UserRole;   // plugin menu name (inactive view rows)
} // anonymous namespace


PluginMenuDialog::PluginMenuDialog(EmbedSession *session, QWidget *parent) :
	QDialog(parent),
	m_session(session)
{
	setWindowTitle(tr("Plugin Options"));
	resize(480, 520);

	auto *const outer = new QVBoxLayout(this);

	m_header = new QLabel(this);
	QFont hf = m_header->font();
	hf.setBold(true);
	m_header->setFont(hf);
	outer->addWidget(m_header);

	m_list = new QListWidget(this);
	m_list->setAlternatingRowColors(true);
	m_list->setUniformItemSizes(true);
	m_list->installEventFilter(this);   // intercept Left/Right/Backspace/typed input
	outer->addWidget(m_list, 1);
	connect(m_list, &QListWidget::itemActivated, this, &PluginMenuDialog::onActivated);
	connect(m_list, &QListWidget::currentRowChanged, this, [this] (int) { updateAdjustBar(); });

	// Mouse-friendly controls acting on the highlighted row: many users reach for
	// the ◀ ▶ glyphs with the mouse, and arrow keys only work while the list has
	// focus.  These buttons do the same as Enter / ← / → / Backspace.
	m_hint = new QLabel(this);
	m_hint->setWordWrap(true);
	outer->addWidget(m_hint);

	auto *const adjust = new QHBoxLayout;
	m_selectBtn = new QPushButton(tr("Select"), this);
	m_leftBtn   = new QPushButton(QStringLiteral("◀"), this);
	m_rightBtn  = new QPushButton(QStringLiteral("▶"), this);
	m_clearBtn  = new QPushButton(tr("Clear"), this);
	m_leftBtn->setToolTip(tr("Decrease the highlighted value (←)"));
	m_rightBtn->setToolTip(tr("Increase the highlighted value (→)"));
	m_clearBtn->setToolTip(tr("Reset / delete the highlighted item (Backspace)"));
	adjust->addWidget(m_selectBtn);
	adjust->addStretch();
	adjust->addWidget(m_leftBtn);
	adjust->addWidget(m_rightBtn);
	adjust->addWidget(m_clearBtn);
	outer->addLayout(adjust);
	connect(m_selectBtn, &QPushButton::clicked, this, [this] {
		if (QListWidgetItem *it = m_list->currentItem()) onActivated(it);
	});
	connect(m_leftBtn,  &QPushButton::clicked, this, [this] { postEvent(m_list->currentRow() + 1, "left"); });
	connect(m_rightBtn, &QPushButton::clicked, this, [this] { postEvent(m_list->currentRow() + 1, "right"); });
	connect(m_clearBtn, &QPushButton::clicked, this, [this] { postEvent(m_list->currentRow() + 1, "clear"); });

	auto *const buttons = new QDialogButtonBox(this);
	m_back = buttons->addButton(tr("Back"), QDialogButtonBox::ActionRole);
	connect(m_back, &QPushButton::clicked, this, &PluginMenuDialog::onBack);
	auto *const close = buttons->addButton(QDialogButtonBox::Close);
	connect(close, &QPushButton::clicked, this, &QDialog::reject);
	outer->addWidget(buttons);

	// poll the worker for navigation/content changes (idle plugins refresh, the
	// open menu changes its item list as the user drills in, etc.)
	m_timer = new QTimer(this);
	m_timer->setInterval(100);
	connect(m_timer, &QTimer::timeout, this, &PluginMenuDialog::tick);
	m_timer->start();

	rebuild();
}


PluginMenuDialog::~PluginMenuDialog() = default;


void PluginMenuDialog::tick()
{
	if (!m_session)
		return;
	unsigned const gen = m_session->pluginMenuGeneration();
	if (gen == m_lastGen)
		return;
	m_lastGen = gen;
	rebuild();
}


void PluginMenuDialog::rebuild()
{
	if (!m_session)
		return;

	EmbedPluginState const st = m_session->pluginMenuSnapshot();
	int const prevRow = m_list->currentRow();

	m_active = st.active;
	m_nokeys = st.nokeys;
	m_items = st.items;

	m_list->clear();
	m_back->setVisible(st.active);

	if (!st.active)
	{
		m_header->setText(tr("Plugin Options"));
		if (st.menus.empty())
		{
			auto *const it = new QListWidgetItem(tr("(no plugin menus available)"), m_list);
			it->setFlags(Qt::NoItemFlags);
		}
		else
		{
			for (const std::string &name : st.menus)
			{
				auto *const it = new QListWidgetItem(QString::fromStdString(name), m_list);
				it->setData(kNameRole, QString::fromStdString(name));
			}
			m_list->setCurrentRow(0);
		}
		return;
	}

	m_header->setText(QString::fromStdString(st.activeName));
	for (const EmbedPluginItem &p : m_items)
	{
		if (p.separator)
		{
			auto *const it = new QListWidgetItem(QString(), m_list);
			it->setFlags(Qt::NoItemFlags);
			it->setSizeHint(QSize(0, 6));
			continue;
		}

		// compose "label    [◀] value [▶]" on a single row
		QString text = QString::fromStdString(p.text);
		if (!p.subtext.empty() || p.leftArrow || p.rightArrow)
		{
			QString val = QString::fromStdString(p.subtext);
			if (p.leftArrow)
				val.prepend(QStringLiteral("◀ "));   // ◀
			if (p.rightArrow)
				val.append(QStringLiteral(" ▶"));      // ▶
			text += QStringLiteral("    ") + val;
		}

		auto *const it = new QListWidgetItem(text, m_list);
		if (p.heading)
		{
			QFont f = it->font();
			f.setBold(true);
			it->setFont(f);
			it->setFlags(Qt::NoItemFlags);
		}
		else if (p.disabled)
		{
			it->setFlags(Qt::NoItemFlags);  // "off": greyed, unselectable
		}
		else
		{
			it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
		}
		if (p.invert)
			it->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
	}

	// restore selection: prefer the worker's suggested 1-based index, else keep
	// the user where they were (clamped) so idle refreshes don't jump the cursor
	int row = -1;
	if (st.selection >= 1 && st.selection <= int(m_items.size()))
		row = st.selection - 1;
	else if (prevRow >= 0 && prevRow < m_list->count())
		row = prevRow;
	if (row >= 0)
		m_list->setCurrentRow(row);

	m_list->setFocus();   // so the keyboard ←/→/Enter work without a click first
	// A "nokeys" menu is polling for raw input (e.g. autofire's Hotkey capture):
	// forward key/button presses to the machine until it finishes.
	setForwarding(m_active && m_nokeys);
	updateAdjustBar();
}


void PluginMenuDialog::setForwarding(bool on)
{
	if (on == m_forwarding)
		return;
	if (on)
		qApp->installEventFilter(this);
	else
		qApp->removeEventFilter(this);
	m_forwarding = on;
}


// Enable the mouse controls to match what the highlighted row supports, and
// show a context-appropriate hint.
void PluginMenuDialog::updateAdjustBar()
{
	bool const showBar = m_active;
	for (QPushButton *b : { m_selectBtn, m_leftBtn, m_rightBtn, m_clearBtn })
		b->setVisible(showBar);

	if (!showBar)
	{
		m_hint->setText(m_session && m_session->pluginMenuSnapshot().menus.empty()
				? QString()
				: tr("Choose a plugin to configure."));
		return;
	}

	int const row = m_list->currentRow();
	EmbedPluginItem const *cur =
			(row >= 0 && row < int(m_items.size())) ? &m_items[row] : nullptr;
	bool const selectable = cur && !cur->separator && !cur->heading && !cur->disabled;

	if (m_forwarding)
	{
		// a poller is running: the buttons don't apply, input is being captured
		for (QPushButton *b : { m_selectBtn, m_leftBtn, m_rightBtn, m_clearBtn })
			b->setEnabled(false);
		m_hint->setText(tr("Press a key or controller button to assign…   (Esc cancels)"));
		return;
	}

	m_selectBtn->setEnabled(selectable);
	m_leftBtn->setEnabled(cur && cur->leftArrow);
	m_rightBtn->setEnabled(cur && cur->rightArrow);
	m_clearBtn->setEnabled(cur != nullptr);
	m_hint->setText(tr("Select opens an item; ◀ ▶ (or ← →) adjust a value; "
			"Clear (or Backspace) resets or deletes."));
}


void PluginMenuDialog::onActivated(QListWidgetItem *item)
{
	if (!item || !m_session)
		return;
	if (!m_active)
	{
		QString const name = item->data(kNameRole).toString();
		if (name.isEmpty())
			return;
		EmbedAction a;
		a.cmd = EmbedCommand::PluginMenuOpen;
		a.sval = name.toStdString();
		m_session->post(a);
		return;
	}
	postEvent(m_list->row(item) + 1, "select");
}


void PluginMenuDialog::onBack()
{
	if (m_active)
		postEvent(m_list->currentRow() + 1, "back");
}


void PluginMenuDialog::postEvent(int index, const char *key)
{
	if (!m_session)
		return;
	EmbedAction a;
	a.cmd = EmbedCommand::PluginMenuEvent;
	a.ival = index;
	a.sval2 = key;
	m_session->post(a);
}


bool PluginMenuDialog::eventFilter(QObject *obj, QEvent *event)
{
	// While a plugin polls for input (a "nokeys" overlay, e.g. autofire's Hotkey
	// capture) forward raw key/mouse events to the machine, app-wide, so the
	// plugin's poller sees them even though this dialog holds the focus.  Esc is
	// forwarded too, so the poller's own UI_CANCEL aborts the capture.
	if (m_forwarding)
	{
		switch (event->type())
		{
		case QEvent::KeyPress:
		case QEvent::KeyRelease:
		{
			auto *const ke = static_cast<QKeyEvent *>(event);
			// Esc must NOT reach the machine: with no MAME menu on the stack it
			// would trigger the in-game exit and quit the game.  Instead back out
			// of the plugin's capture menu (cancelling the assignment).
			if (ke->key() == Qt::Key_Escape)
			{
				if (event->type() == QEvent::KeyPress)
					postEvent(m_list->currentRow() + 1, "back");
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
			return true;
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
		return QDialog::eventFilter(obj, event);
	}

	if (obj == m_list && event->type() == QEvent::KeyPress && m_active)
	{
		auto *const ke = static_cast<QKeyEvent *>(event);
		int const row = m_list->currentRow();
		EmbedPluginItem const *cur =
				(row >= 0 && row < int(m_items.size())) ? &m_items[row] : nullptr;

		switch (ke->key())
		{
		case Qt::Key_Left:
			if (cur && cur->leftArrow) { postEvent(row + 1, "left"); return true; }
			break;
		case Qt::Key_Right:
			if (cur && cur->rightArrow) { postEvent(row + 1, "right"); return true; }
			break;
		case Qt::Key_Backspace:
		case Qt::Key_Delete:
			postEvent(row + 1, "clear");
			return true;
		case Qt::Key_Up:
		case Qt::Key_Down:
		case Qt::Key_Return:
		case Qt::Key_Enter:
		case Qt::Key_Escape:
			break;   // let the list / dialog handle navigation, activation, close
		default:
			// printable characters drive plugins that accept typed input (e.g. the
			// cheat finder's value entry) unless the menu opted out with "nokeys"
			if (!m_nokeys && !ke->text().isEmpty())
			{
				bool sent = false;
				for (QChar const ch : ke->text())
				{
					if (ch.isPrint())
					{
						postEvent(row + 1, std::to_string(unsigned(ch.unicode())).c_str());
						sent = true;
					}
				}
				if (sent)
					return true;
			}
			break;
		}
	}
	return QDialog::eventFilter(obj, event);
}


void PluginMenuDialog::reject()
{
	setForwarding(false);   // never leave an app-wide event filter installed
	if (m_session)
	{
		EmbedAction a;
		a.cmd = EmbedCommand::PluginMenuClose;
		m_session->post(a);
	}
	QDialog::reject();
}


void PluginMenuDialog::closeEvent(QCloseEvent *event)
{
	setForwarding(false);
	if (m_session)
	{
		EmbedAction a;
		a.cmd = EmbedCommand::PluginMenuClose;
		m_session->post(a);
	}
	QDialog::closeEvent(event);
}

} // namespace osd::qtui

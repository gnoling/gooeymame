// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  audioeffectsdialog.cpp - in-game audio effects editor for the qtui OSD
//
//============================================================

#include "audioeffectsdialog.h"

#include "embedsession.h"

#include <QtCore/QTimer>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSlider>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#include <climits>
#include <cmath>

namespace osd::qtui {

namespace {

// number of integer slider positions covering [minv, maxv] at the given step
int sliderSteps(double minv, double maxv, double step)
{
	if (step <= 0.0)
		step = 0.01;
	int const n = int(std::lround((maxv - minv) / step));
	return (n < 1) ? 1 : n;
}

int valueToPos(double value, double minv, double step)
{
	if (step <= 0.0)
		step = 0.01;
	return int(std::lround((value - minv) / step));
}

double posToValue(int pos, double minv, double step)
{
	return minv + double(pos) * step;
}

} // anonymous namespace


AudioEffectsDialog::AudioEffectsDialog(EmbedSession *session, QWidget *parent) :
	QDialog(parent),
	m_session(session)
{
	setWindowTitle(tr("Audio Effects"));
	resize(620, 680);

	auto *const outer = new QVBoxLayout(this);

	m_scroll = new QScrollArea(this);
	m_scroll->setWidgetResizable(true);
	outer->addWidget(m_scroll, 1);

	auto *const buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	outer->addWidget(buttons);

	// poll the worker for effect-chain changes (resets, presets, or MAME's own UI)
	m_timer = new QTimer(this);
	m_timer->setInterval(100);
	connect(m_timer, &QTimer::timeout, this, &AudioEffectsDialog::tick);
	m_timer->start();

	rebuild();
}


AudioEffectsDialog::~AudioEffectsDialog() = default;


std::string AudioEffectsDialog::signature() const
{
	std::string sig;
	if (!m_session)
		return sig;
	for (const EmbedEffect &ef : m_session->audioEffectsSnapshot())
	{
		sig += 'c'; sig += std::to_string(ef.chain);
		sig += 'i'; sig += std::to_string(ef.index);
		sig += 't'; sig += std::to_string(ef.type);
		for (const EmbedEffectParam &p : ef.params)
		{
			sig += "p"; sig += std::to_string(p.id);
			sig += "k"; sig += std::to_string(p.kind);
			sig += "n"; sig += std::to_string(p.choices.size());
		}
		sig += '|';
	}
	return sig;
}


void AudioEffectsDialog::postParam(int chain, int entry, int paramId, double value)
{
	if (!m_session)
		return;
	EmbedAction a;
	a.cmd = EmbedCommand::SetEffectParam;
	a.mask = std::uint32_t(chain);
	a.value = std::uint32_t(entry);
	a.ival = paramId;
	a.dval = value;
	m_session->post(a);
}

void AudioEffectsDialog::postResetParam(int chain, int entry, int paramId)
{
	if (!m_session)
		return;
	EmbedAction a;
	a.cmd = EmbedCommand::ResetEffectParam;
	a.mask = std::uint32_t(chain);
	a.value = std::uint32_t(entry);
	a.ival = paramId;
	m_session->post(a);
}

void AudioEffectsDialog::postResetEffect(int chain, int entry)
{
	if (!m_session)
		return;
	EmbedAction a;
	a.cmd = EmbedCommand::ResetEffect;
	a.mask = std::uint32_t(chain);
	a.value = std::uint32_t(entry);
	m_session->post(a);
}


void AudioEffectsDialog::rebuild()
{
	m_rows.clear();

	auto *const content = new QWidget;
	auto *const vbox = new QVBoxLayout(content);

	std::vector<EmbedEffect> const effects =
			m_session ? m_session->audioEffectsSnapshot() : std::vector<EmbedEffect>();
	m_lastGen = m_session ? m_session->audioEffectsGeneration() : 0;
	m_signature = signature();

	if (effects.empty())
	{
		vbox->addWidget(new QLabel(tr("This machine has no configurable audio effects.")));
		vbox->addStretch();
		m_scroll->setWidget(content);
		return;
	}

	int prevChain = INT_MIN;
	for (const EmbedEffect &ef : effects)
	{
		if (ef.chain != prevChain)
		{
			prevChain = ef.chain;
			QString const title = ef.chainDefault
					? tr("Default chain (applies where a speaker has no override)")
					: tr("Speaker: %1").arg(QString::fromStdString(ef.chainTag));
			auto *const hdr = new QLabel(title);
			QFont f = hdr->font();
			f.setBold(true);
			hdr->setFont(f);
			vbox->addWidget(hdr);
		}

		auto *const box = new QGroupBox(QString::fromStdString(ef.typeName), content);
		auto *const grid = new QGridLayout(box);
		grid->setColumnStretch(1, 1);

		int gridRow = 0;
		auto *const resetAll = new QPushButton(tr("Reset all"), box);
		resetAll->setToolTip(tr("Reset this effect to its defaults"));
		grid->addWidget(resetAll, gridRow, 0, 1, 4, Qt::AlignRight);
		{
			int const chain = ef.chain, entry = ef.index;
			connect(resetAll, &QPushButton::clicked, this,
					[this, chain, entry] { postResetEffect(chain, entry); });
		}
		++gridRow;

		std::string prevGroup = "\x01";   // sentinel so the first group always prints
		for (const EmbedEffectParam &p : ef.params)
		{
			if (p.group != prevGroup)
			{
				prevGroup = p.group;
				if (!p.group.empty())
				{
					auto *const sub = new QLabel(QString::fromStdString(p.group), box);
					QFont f = sub->font();
					f.setItalic(true);
					sub->setFont(f);
					grid->addWidget(sub, gridRow, 0, 1, 4);
					++gridRow;
				}
			}

			Row row;
			row.chain = ef.chain;
			row.entry = ef.index;
			row.paramId = p.id;
			row.kind = p.kind;
			row.minv = p.minv;
			row.step = p.step;

			auto *const nameLbl = new QLabel(QString::fromStdString(p.label), box);
			row.name = nameLbl;
			grid->addWidget(nameLbl, gridRow, 0);

			int const chain = ef.chain, entry = ef.index, pid = p.id;

			if (p.kind == EmbedEffectParam::Toggle)
			{
				auto *const cb = new QCheckBox(box);
				cb->setText(p.value != 0
						? QString::fromStdString(p.choices.size() > 1 ? p.choices[1] : std::string("On"))
						: QString::fromStdString(p.choices.size() > 0 ? p.choices[0] : std::string("Off")));
				cb->setChecked(p.value != 0);
				row.check = cb;
				grid->addWidget(cb, gridRow, 1, 1, 2);
				std::vector<std::string> const choices = p.choices;
				connect(cb, &QCheckBox::toggled, this,
						[this, chain, entry, pid, cb, choices] (bool on) {
							if (m_updating)
								return;
							cb->setText(QString::fromStdString(
									on ? (choices.size() > 1 ? choices[1] : std::string("On"))
									   : (choices.size() > 0 ? choices[0] : std::string("Off"))));
							postParam(chain, entry, pid, on ? 1.0 : 0.0);
						});
			}
			else if (p.kind == EmbedEffectParam::Choice)
			{
				auto *const combo = new QComboBox(box);
				for (const std::string &c : p.choices)
					combo->addItem(QString::fromStdString(c));
				combo->setCurrentIndex(int(p.value));
				row.combo = combo;
				grid->addWidget(combo, gridRow, 1, 1, 2);
				connect(combo, QOverload<int>::of(&QComboBox::activated), this,
						[this, chain, entry, pid] (int idx) {
							if (m_updating)
								return;
							postParam(chain, entry, pid, double(idx));
						});
			}
			else // Numeric
			{
				auto *const slider = new QSlider(Qt::Horizontal, box);
				slider->setMinimum(0);
				slider->setMaximum(sliderSteps(p.minv, p.maxv, p.step));
				slider->setValue(valueToPos(p.value, p.minv, p.step));
				row.slider = slider;
				grid->addWidget(slider, gridRow, 1);

				auto *const val = new QLabel(QString::fromStdString(p.text), box);
				val->setMinimumWidth(80);
				val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
				row.value = val;
				grid->addWidget(val, gridRow, 2);

				double const minv = p.minv, step = p.step;
				connect(slider, &QSlider::valueChanged, this,
						[this, chain, entry, pid, minv, step] (int pos) {
							if (m_updating)
								return;
							postParam(chain, entry, pid, posToValue(pos, minv, step));
						});
			}

			auto *const reset = new QToolButton(box);
			reset->setText(QStringLiteral("↺"));   // ↺
			reset->setToolTip(tr("Reset to default"));
			row.name = nameLbl;
			grid->addWidget(reset, gridRow, 3);
			connect(reset, &QToolButton::clicked, this,
					[this, chain, entry, pid] { postResetParam(chain, entry, pid); });

			// deemphasize parameters currently inheriting the default
			nameLbl->setEnabled(!p.isDefault);

			m_rows.push_back(row);
			++gridRow;
		}

		vbox->addWidget(box);
	}

	vbox->addStretch();
	m_scroll->setWidget(content);
}


void AudioEffectsDialog::refreshValues()
{
	if (!m_session)
		return;

	// flatten the snapshot params in the same order rebuild() built the rows
	std::vector<EmbedEffectParam> flat;
	for (const EmbedEffect &ef : m_session->audioEffectsSnapshot())
		for (const EmbedEffectParam &p : ef.params)
			flat.push_back(p);

	if (flat.size() != m_rows.size())
	{
		rebuild();   // structure drifted unexpectedly
		return;
	}

	m_updating = true;
	for (std::size_t i = 0; i < m_rows.size(); ++i)
	{
		Row &r = m_rows[i];
		const EmbedEffectParam &p = flat[i];
		if (r.name)
			r.name->setEnabled(!p.isDefault);
		if (r.check)
		{
			r.check->setChecked(p.value != 0);
			r.check->setText(QString::fromStdString(
					p.value != 0 ? (p.choices.size() > 1 ? p.choices[1] : std::string("On"))
					             : (p.choices.size() > 0 ? p.choices[0] : std::string("Off"))));
		}
		if (r.combo)
			r.combo->setCurrentIndex(int(p.value));
		if (r.slider)
			r.slider->setValue(valueToPos(p.value, r.minv, r.step));
		if (r.value)
			r.value->setText(QString::fromStdString(p.text));
	}
	m_updating = false;
}


void AudioEffectsDialog::tick()
{
	if (!m_session)
		return;
	unsigned const gen = m_session->audioEffectsGeneration();
	if (gen == m_lastGen)
		return;
	m_lastGen = gen;
	if (signature() != m_signature)
		rebuild();        // effect/param layout changed (e.g. EQ shelf, preset)
	else
		refreshValues();  // same layout — update values in place
}

} // namespace osd::qtui

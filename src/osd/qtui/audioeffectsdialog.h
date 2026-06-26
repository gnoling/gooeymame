// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  audioeffectsdialog.h - in-game audio effects editor for the qtui OSD
//
//  Lists each speaker's (and the default) audio-effect chain — Filter,
//  Compressor, EQ, Reverb — with a live, generic control per parameter
//  (toggle / choice / slider).  The worker (EmbedController) publishes typed
//  parameter descriptors; this dialog renders them and posts changes back to
//  the emulation thread, which applies them to the live sound_manager.  Edits
//  are live: dragging a slider changes the sound immediately.  A per-parameter
//  reset restores its default; a per-effect "Reset all" resets the effect.
//
//============================================================
#ifndef MAME_OSD_QTUI_AUDIOEFFECTSDIALOG_H
#define MAME_OSD_QTUI_AUDIOEFFECTSDIALOG_H

#pragma once

#include <QtWidgets/QDialog>

#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QScrollArea;
class QSlider;
class QTimer;
class QVBoxLayout;
class QWidget;

namespace osd::qtui {

class EmbedSession;

class AudioEffectsDialog : public QDialog
{
	Q_OBJECT

public:
	explicit AudioEffectsDialog(EmbedSession *session, QWidget *parent = nullptr);
	~AudioEffectsDialog();

private:
	// One built parameter row, kept so a refresh can update it in place (so a
	// slider drag isn't disrupted by the worker echoing the value back).
	struct Row
	{
		int         chain = 0;
		int         entry = 0;
		int         paramId = 0;
		int         kind = 0;
		double      minv = 0, step = 0.01;
		QCheckBox  *check = nullptr;
		QComboBox  *combo = nullptr;
		QSlider    *slider = nullptr;
		QLabel     *value = nullptr;
		QLabel     *name = nullptr;
	};

	void rebuild();           // tear down + build the whole form from the snapshot
	void refreshValues();     // update existing rows in place (no structure change)
	void tick();              // poll the worker for generation changes
	std::string signature() const;  // structural fingerprint to detect layout changes

	void postParam(int chain, int entry, int paramId, double value);
	void postResetParam(int chain, int entry, int paramId);
	void postResetEffect(int chain, int entry);

	EmbedSession *m_session;
	QScrollArea  *m_scroll = nullptr;
	QTimer       *m_timer = nullptr;
	std::vector<Row> m_rows;
	unsigned      m_lastGen = 0;
	std::string   m_signature;
	bool          m_updating = false;   // suppress control signals during in-place refresh
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_AUDIOEFFECTSDIALOG_H

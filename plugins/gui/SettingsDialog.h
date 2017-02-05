#pragma once

#include <QDialog>
#include <QTabWidget>
#include "Plugin.h"

class CharacterWidget;

class SettingsDialog: public QDialog {
	Q_OBJECT
public:
	explicit SettingsDialog(CharacterWidget *parent);

private:
	CharacterWidget *m_characterWidget;
	PluginClass *m_plugin;
	QTabWidget *m_tabWidget;

	void initUI();

};
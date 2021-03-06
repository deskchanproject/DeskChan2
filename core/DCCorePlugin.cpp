#include <QDebug>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QVariant>
#include "DCPluginManager.h"
#include "DCExternalPlugin.h"
#include "DCCorePlugin.h"

DCCorePlugin::DCCorePlugin(DCPluginManager &pluginManager): DCPlugin(pluginManager) {
	initialize("core");
	initCallbacks();
	loadPlugin("python_support");
	if (QCoreApplication::instance()->property("guiSupportAvailable").toBool()) {
		loadPlugin("gui");
	}
	loadPlugin("random_phrases");
}

DCCorePlugin::~DCCorePlugin() {
}

bool DCCorePlugin::loadPlugin(const QString &name) {
	QString pluginDirName = pluginDir(name);
	if (pluginDirName.isEmpty()) {
		return false;
	}
	static const QStringList librarySuffixes = {
#if defined(_WIN32) || defined(WIN32)
			".dll", ".DLL",
#elif defined(__APPLE__) && defined(TARGET_OS_MAC)
			".dylib", ".so",
#elif defined(__linux__)
			".so",
#else
#error "Unsupported OS"
#endif
			"\\/" // Invalid file name suffix
	};
	QString pluginLibraryName;
	for (auto& suffix : librarySuffixes) {
		pluginLibraryName = pluginDirName + QDir::separator() + name + suffix;
		if (QFileInfo(pluginLibraryName).isReadable()) break;
		pluginLibraryName = pluginDirName + QDir::separator() + "lib" + name + suffix;
		if (QFileInfo(pluginLibraryName).isReadable()) break;
	}
	if (QLibrary::isLibrary(pluginLibraryName)) {
		QFileInfo libraryFileInfo(pluginLibraryName);
		QLibrary *library = new QLibrary(libraryFileInfo.dir().absoluteFilePath(libraryFileInfo.completeBaseName()));
		library->setLoadHints(QLibrary::ResolveAllSymbolsHint);
		if (library->load()) {
			auto createPluginObjectFunc = (DCPlugin *(*)(DCPluginManager &)) library->resolve(
					DC_LOADABLE_PLUGIN_FUNC_NAME);
			if (createPluginObjectFunc) {
				DCPlugin *plugin = createPluginObjectFunc(pluginManager());
				Q_ASSERT(plugin != nullptr);
				plugin->m_sharedLibrary = library;
				return true;
			}
			library->unload();
		}
		qWarning() << library->errorString();
		delete library;
	}
	pluginLibraryName = pluginDirName + QDir::separator() + name + ".cmd";
	if (QFileInfo(pluginLibraryName).isReadable()) {
		QFile commandFile(pluginLibraryName);
		if (commandFile.open(QIODevice::ReadOnly)) {
			QString command = QString::fromUtf8(commandFile.readLine()).trimmed();
			commandFile.close();
			if (command.size() > 0) {
				new DCExternalPlugin(command, pluginDirName, m_pluginManager);
				return true;
			}
		}
	}
	for (auto &loaderInfo : m_pluginLoaders) {
		pluginLibraryName = pluginDirName + QDir::separator() + loaderInfo.pattern.arg(name);
		if (QFileInfo(pluginLibraryName).isReadable()) {
			sendMessage(loaderInfo.tag, pluginLibraryName);
			return true;
		}
	}
	return false;
}

QString DCCorePlugin::pluginDir(const DCPlugin *plugin) const {
	return pluginDir(plugin->id());
}

QString DCCorePlugin::pluginDir(const QString &id) const {
	QString baseDir = QCoreApplication::applicationDirPath();
	QString dir = baseDir + QDir::separator() + "plugins" + QDir::separator() + id;
	if (QFileInfo(dir).isDir()) {
		return dir;
	}
	return QString();
}

QString DCCorePlugin::pluginDataDir(const DCPlugin *plugin) const {
	return pluginDataDir(plugin->id());
}

QString DCCorePlugin::pluginDataDir(const QString &id) const {
	QString baseDirName = QCoreApplication::applicationDirPath();
	QString dataDirName = baseDirName + QDir::separator() + "data";
	if (!QFileInfo(dataDirName).isDir()) {
		baseDirName = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		dataDirName = baseDirName + QDir::separator() + "data";
	}
	QString pluginDataDirName = dataDirName + QDir::separator() + id;
	QDir dir(pluginDataDirName);
	if (!dir.exists()) {
		dir.mkpath(".");
		qDebug() << "Directory" << pluginDataDirName << "created";
	}
	return pluginDataDirName;
}

void DCCorePlugin::initCallbacks() {
	subscribe("core:quit", [](const QString &sender, const QString &tag, const QVariant &data) {
		qDebug() << "Plugin" << sender << "requested application termination";
		QCoreApplication::quit();
	});
	subscribe("core:load-plugin", [this](const QString &sender, const QString &tag, const QVariant &data) {
		loadPlugin(data.toString());
	});
	subscribe("core:query-plugin-data-dir", [this](const QString &sender, const QString &tag, const QVariant &data) {
		int seq = data.toMap()["seq"].toInt();
		QString dirName = pluginDataDir(sender);
		sendMessage(sender, QMap<QString, QVariant>({{"seq", seq}, {"path", dirName}}));
	});
	subscribe("core:query-plugin-dir", [this](const QString &sender, const QString &tag, const QVariant &data) {
		int seq = data.toMap()["seq"].toInt();
		QString dirName = pluginDir(sender);
		sendMessage(sender, QMap<QString, QVariant>({{"seq", seq}, {"path", dirName}}));
	});
	subscribe("core:register-alternative", [this](const QString &sender, const QString &tag, const QVariant &data) {
		auto m = data.toMap();
		QString srcTag = m["srcTag"].toString();
		QString dstTag = m["dstTag"].toString();
		int priority = m["priority"].toInt();
		auto it = m_alternatives.find(srcTag);
		if (it == m_alternatives.end()) {
			it = m_alternatives.insert(srcTag, QList<AlternativeInfo>());
		}
		auto it2 = it->begin();
		while ((it2 != it->end()) && (it2->priority > priority)) {
			++it2;
		}
		it->insert(it2, AlternativeInfo(priority, dstTag, sender));
		qDebug() << "Registered alternative" << dstTag << "for tag" << srcTag << "by plugin" << sender;
		if (it->size() == 1) {
			subscribe(srcTag, [this](const QString &sender_, const QString &tag_, const QVariant &data_) {
				DCPlugin *senderPlugin = m_pluginManager.plugin(sender_);
				if (senderPlugin) {
					auto it3 = m_alternatives.find(tag_);
					if (it3 != m_alternatives.end()) {
						auto it4 = it3->begin();
						while ((it4 != it3->end()) && (it4->priority == it3->begin()->priority)) {
							m_pluginManager.sendMessage(senderPlugin, it4->tag, data_);
							++it4;
						}
					}
				}
			});
		}
	});
	subscribe("core:unregister-alternative", [this](const QString &sender, const QString &tag, const QVariant &data) {
		auto m = data.toMap();
		QString srcTag = m["srcTag"].toString();
		QString dstTag = m["dstTag"].toString();
		auto it = m_alternatives.find(srcTag);
		if (it != m_alternatives.end()) {
			for (auto it2 = it->begin(); it2 != it->end(); ++it2) {
				if ((it2->tag == dstTag) && (it2->pluginId == sender)) {
					qDebug() << "Unregistered alternative" << dstTag << "for tag" << srcTag;
					it->erase(it2);
					break;
				}
			}
			if (it->size() == 0) {
				qDebug() << "No more alternatives for" << srcTag;
				m_alternatives.erase(it);
				unsubscribe(it.key());
			}
		}
	});
	subscribe("core:change-alternative-priority", [this](const QString &sender, const QString &tag, const QVariant &data) {
		auto m = data.toMap();
		QString srcTag = m["srcTag"].toString();
		QString dstTag = m["dstTag"].toString();
		int priority = m["priority"].toInt();
		auto it = m_alternatives.find(srcTag);
		if (it != m_alternatives.end()) {
			for (auto it2 = it->begin(); it2 != it->end(); ++it2) {
				if (it2->tag == dstTag) {
					QString pluginId = it2->pluginId;
					it->erase(it2);
					it2 = it->begin();
					while ((it2 != it->begin()) && (it2->priority > priority)) {
						++it2;
					}
					it->insert(it2, AlternativeInfo(priority, dstTag, pluginId));
					qDebug() << "Changed priority to" << priority << "for alternative" << dstTag << "for tag" << srcTag;
					break;
				}
			}
		}
	});
	subscribe("core:query-alternatives", [this](const QString &sender, const QString &tag, const QVariant &data) {
		int seq = data.toMap()["seq"].toInt();
		QVariantMap m;
		for (auto it = m_alternatives.begin(); it != m_alternatives.end(); ++it) {
			QVariantList l;
			for (auto it2 = it->begin(); it2 != it->end(); ++it2) {
				QVariantMap m2;
				m2["priority"] = it2->priority;
				m2["tag"] = it2->tag;
				m2["pluginId"] = it2->pluginId;
				l.append(m2);
			}
			m[it.key()] = l;
		}
		sendMessage(sender, QVariantMap({{"seq", seq}, {"alternatives", m}}));
	});
	subscribe("core-events:plugin-unload", [this](const QString &sender, const QString &tag, const QVariant &data) {
		QString pluginId = data.toString();
		for (auto it = m_alternatives.begin(); it != m_alternatives.end(); ++it) {
			for (auto it2 = it->begin(); it2 != it->end(); ++it2) {
				if (it2->pluginId == pluginId) {
					qDebug() << "Unregistered alternative" << it2->tag << "for" << it.key();
					it->erase(it2);
					break;
				}
			}
			//if (it->size() == 0) {
			//	qDebug() << "No more alternatives for" << it.key();
			//}
		}
		auto it = m_pluginLoaders.begin();
		while (it != m_pluginLoaders.end()) {
			if (it->pluginId == pluginId) {
				qDebug() << "Unregistered plugin loader for pattern" << it->pattern;
				it = m_pluginLoaders.erase(it);
			} else {
				++it;
			}
		}
	});
	subscribe("core:register-plugin-loader", [this](const QString &sender, const QString &tag, const QVariant &data) {
		QVariantMap m = data.toMap();
		QString pattern = m["pattern"].toString();
		QString dstTag = m["tag"].toString();
		m_pluginLoaders.append(PluginLoaderInfo(pattern, dstTag, sender));
		qDebug() << "Registered plugin loader for pattern" << pattern << "by plugin" << sender;
	});
}

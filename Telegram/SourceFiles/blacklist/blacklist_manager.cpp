/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "blacklist/blacklist_manager.h"

#include "base/debug_log.h"
#include "data/data_user.h"
#include "history/history_item.h"
#include "lang/lang_instance.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

#include <algorithm>

namespace Blacklist {
namespace {

constexpr auto kFileName = "blacklist.json";

[[nodiscard]] bool IsUsernameChar(QChar ch) {
	return (ch >= u'a' && ch <= u'z')
		|| (ch >= u'A' && ch <= u'Z')
		|| (ch >= u'0' && ch <= u'9')
		|| (ch == u'_');
}

} // namespace

Manager &Manager::Instance() {
	static auto instance = Manager();
	return instance;
}

Manager::Manager() = default;

QString Manager::storagePath() const {
	if (cWorkingDir().isEmpty()) {
		return QString();
	}
	return cWorkingDir() + u"tdata/"_q + kFileName;
}

void Manager::ensureLoaded() const {
	if (_loaded) {
		return;
	}
	const auto path = storagePath();
	if (path.isEmpty()) {
		// The launcher has not selected the working directory yet.
		return;
	}
	_loaded = true;

	auto file = QFile(path);
	if (!file.exists()) {
		return;
	} else if (!file.open(QIODevice::ReadOnly)) {
		LOG(("Blacklist: Could not read %1.").arg(path));
		return;
	}

	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		LOG(("Blacklist: Could not parse %1.").arg(path));
		return;
	}
	const auto root = document.object();
	_enabled = root.value(u"enabled"_q).toBool(true);
	const auto users = root.value(u"users"_q).toArray();
	for (const auto &value : users) {
		const auto object = value.toObject();
		const auto id = UserId(
			object.value(u"id"_q).toString().toULongLong());
		if (!id) {
			continue;
		} else if (_ids.contains(id)) {
			continue;
		}
		auto entry = Entry{
			.id = id,
			.username = object.value(u"username"_q).toString(),
			.name = object.value(u"name"_q).toString(),
		};
		_entries.push_back(std::move(entry));
		_ids.emplace(id);
	}
}

void Manager::save() const {
	const auto path = storagePath();
	if (path.isEmpty()) {
		return;
	}
	QDir().mkpath(QFileInfo(path).absolutePath());

	auto users = QJsonArray();
	for (const auto &entry : _entries) {
		users.append(QJsonObject{
			{ u"id"_q, QString::number(entry.id.bare) },
			{ u"username"_q, entry.username },
			{ u"name"_q, entry.name },
		});
	}
	const auto document = QJsonDocument(QJsonObject{
		{ u"enabled"_q, _enabled },
		{ u"users"_q, users },
	});

	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("Blacklist: Could not write %1.").arg(path));
		return;
	}
	file.write(document.toJson(QJsonDocument::Indented));
	if (!file.commit()) {
		LOG(("Blacklist: Could not commit %1.").arg(path));
	}
}

void Manager::changed() const {
	++_version;
	_changes.fire({});
}

bool Manager::enabled() const {
	ensureLoaded();
	return _enabled;
}

void Manager::setEnabled(bool enabled) {
	ensureLoaded();
	if (_enabled == enabled) {
		return;
	}
	_enabled = enabled;
	save();
	changed();
}

const std::vector<Entry> &Manager::entries() const {
	ensureLoaded();
	return _entries;
}

bool Manager::contains(UserId id) const {
	ensureLoaded();
	return _ids.contains(id);
}

bool Manager::add(UserId id, const QString &username, const QString &name) {
	ensureLoaded();
	if (!id) {
		return false;
	}
	for (auto &entry : _entries) {
		if (entry.id != id) {
			continue;
		}
		if (entry.username == username && entry.name == name) {
			return false;
		}
		entry.username = username;
		entry.name = name;
		save();
		changed();
		return true;
	}
	_entries.push_back(Entry{
		.id = id,
		.username = username,
		.name = name,
	});
	_ids.emplace(id);
	save();
	changed();
	return true;
}

bool Manager::remove(UserId id) {
	ensureLoaded();
	const auto i = std::find_if(
		_entries.begin(),
		_entries.end(),
		[=](const Entry &entry) { return entry.id == id; });
	if (i == _entries.end()) {
		return false;
	}
	_entries.erase(i);
	_ids.erase(id);
	save();
	changed();
	return true;
}

rpl::producer<> Manager::changes() const {
	return _changes.events();
}

uint64 Manager::version() const {
	return _version;
}

void Manager::reload() {
	_loaded = false;
	_enabled = true;
	_entries.clear();
	_ids.clear();
	ensureLoaded();
	changed();
}

bool IsHidden(UserId id) {
	const auto &manager = Manager::Instance();
	return manager.enabled() && manager.contains(id);
}

bool IsHidden(UserData *user) {
	return user && IsHidden(peerToUser(user->id));
}

bool IsHidden(const HistoryItem *item) {
	if (!item) {
		return false;
	}
	const auto &manager = Manager::Instance();
	if (!manager.enabled() || manager.entries().empty()) {
		return false;
	}
	const auto from = item->from();
	if (const auto user = from->asUser()) {
		if (manager.contains(peerToUser(user->id))) {
			return true;
		}
	}
	const auto author = item->author();
	if (author != from) {
		if (const auto user = author->asUser()) {
			if (manager.contains(peerToUser(user->id))) {
				return true;
			}
		}
	}
	return false;
}

QString NormalizeUsername(const QString &input) {
	auto result = input.trimmed();
	if (result.startsWith(u"https://t.me/"_q, Qt::CaseInsensitive)) {
		result = result.mid(13);
	} else if (result.startsWith(u"http://t.me/"_q, Qt::CaseInsensitive)) {
		result = result.mid(12);
	} else if (result.startsWith(u"t.me/"_q, Qt::CaseInsensitive)) {
		result = result.mid(5);
	}
	while (result.startsWith(u'@')) {
		result.remove(0, 1);
	}
	for (auto i = 0; i != result.size(); ++i) {
		if (!IsUsernameChar(result[i])) {
			result.truncate(i);
			break;
		}
	}
	return result;
}

bool IsValidUsername(const QString &normalized) {
	if (normalized.size() < 4 || normalized.size() > 32) {
		return false;
	}
	for (const auto ch : normalized) {
		if (!IsUsernameChar(ch)) {
			return false;
		}
	}
	return true;
}

QString Tr(const char *english, const char *russian) {
	const auto id = Lang::GetInstance().id();
	const auto useRussian = id.startsWith(u"ru"_q, Qt::CaseInsensitive);
	return QString::fromUtf8(useRussian ? russian : english);
}

} // namespace Blacklist

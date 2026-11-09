/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_set.h"
#include "data/data_peer_id.h"

#include <rpl/event_stream.h>
#include <rpl/producer.h>

#include <vector>

class HistoryItem;
class UserData;

namespace Blacklist {

struct Entry {
	UserId id;
	QString username;
	QString name;

	friend inline bool operator==(const Entry &, const Entry &) = default;
};

class Manager final {
public:
	static Manager &Instance();

	[[nodiscard]] bool enabled() const;
	void setEnabled(bool enabled);

	[[nodiscard]] const std::vector<Entry> &entries() const;
	[[nodiscard]] bool contains(UserId id) const;

	// Returns true if the list was changed, false if the entry already
	// existed with the same data.
	bool add(UserId id, const QString &username, const QString &name);
	bool remove(UserId id);

	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] uint64 version() const;

	// Re-reads the list from disk. Used by tests and after an external
	// modification of the file.
	void reload();

private:
	Manager();

	void ensureLoaded() const;
	void save() const;
	[[nodiscard]] QString storagePath() const;
	void changed() const;

	mutable bool _loaded = false;
	mutable bool _enabled = true;
	mutable std::vector<Entry> _entries;
	mutable base::flat_set<UserId> _ids;
	mutable uint64 _version = 0;
	mutable rpl::event_stream<> _changes;

};

[[nodiscard]] bool IsHidden(UserId id);
[[nodiscard]] bool IsHidden(UserData *user);
[[nodiscard]] bool IsHidden(const HistoryItem *item);

[[nodiscard]] QString NormalizeUsername(const QString &input);
[[nodiscard]] bool IsValidUsername(const QString &normalized);

// A small localization helper for the custom UI. The official translations
// are loaded from Telegram and cannot contain our custom keys, so we ship
// the two most useful languages directly.
[[nodiscard]] QString Tr(const char *english, const char *russian);

} // namespace Blacklist

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_blacklist.h"

#include "apiwrap.h"
#include "blacklist/blacklist_manager.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"
#include "settings/sections/settings_main.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <crl/crl_on_main.h>
#include <rpl/variable.h>

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using namespace Builder;

void BuildBlacklistSection(SectionBuilder &builder) {
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"blacklist/add"_q,
			.title = Blacklist::Tr("Blacklist", "Чёрный список"),
			.keywords = { u"blacklist"_q, u"hide"_q, u"ignore"_q },
		};
	});
}

class BlacklistSection final : public Section<BlacklistSection> {
public:
	BlacklistSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

const auto kMeta = BuildHelper({
	.id = BlacklistSection::Id(),
	.parentId = MainId(),
	.title = &tr::lng_settings_blacklist,
	.icon = &st::menuIconBlock,
}, [](SectionBuilder &builder) {
	BuildBlacklistSection(builder);
});

BlacklistSection::BlacklistSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> BlacklistSection::title() {
	return rpl::single(Blacklist::Tr("Blacklist", "Чёрный список"));
}

void BlacklistSection::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto manager = &Blacklist::Manager::Instance();

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			rpl::single(Blacklist::Tr(
				"Messages from users in this list are hidden locally in all "
				"chats. They are not deleted from Telegram servers, and "
				"other participants' messages are shown as usual.",
				"Сообщения пользователей из этого списка скрываются локально "
				"во всех чатах. Они не удаляются с серверов Telegram, "
				"сообщения других участников отображаются как обычно.")),
			st::changePhoneDescription),
		st::boxRowPadding,
		style::al_top);

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	const auto enabled = content->lifetime().make_state<rpl::variable<bool>>(
		manager->enabled());
	const auto toggle = content->add(
		object_ptr<Ui::SettingsButton>(
			content,
			rpl::single(Blacklist::Tr(
				"Enable the filter",
				"Включить фильтр")),
			st::settingsButtonNoIcon));
	toggle->toggleOn(enabled->value());
	toggle->toggledChanges() | rpl::on_next([=](bool value) {
		if (manager->enabled() != value) {
			manager->setEnabled(value);
		}
		*enabled = manager->enabled();
	}, toggle->lifetime());

	Ui::AddDividerText(content, rpl::single(Blacklist::Tr(
		"Changing the list immediately updates all open chats and "
		"saved history. Removing a user makes their messages visible again.",
		"Изменение списка сразу обновляет все открытые чаты и сохранённую "
		"историю. После удаления пользователя его сообщения снова видны.")));

	const auto field = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::defaultInputField,
			Ui::InputField::Mode::SingleLine,
			rpl::single(Blacklist::Tr(
				"@username",
				"@username")),
			QString()),
		st::boxRowPadding);
	field->setInputMethodHints(
		Qt::ImhLatinOnly
		| Qt::ImhNoAutoUppercase
		| Qt::ImhNoPredictiveText);

	const auto status = content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			rpl::single(QString()),
			st::changePhoneDescription),
		st::boxRowPadding,
		style::al_top);

	const auto submit = [=] {
		const auto username = Blacklist::NormalizeUsername(
			field->getLastText());
		if (!Blacklist::IsValidUsername(username)) {
			field->showError();
			status->setText(Blacklist::Tr(
				"Enter a username like @username.",
				"Введите имя пользователя вида @username."));
			return;
		}
		field->hideError();
		status->setText(Blacklist::Tr(
			"Looking for the user...",
			"Ищу пользователя..."));

		controller()->session().api().request(
			MTPcontacts_ResolveUsername(
				MTP_flags(0),
				MTP_string(username),
				MTP_string())
		).done(crl::guard(this, [=](
				const MTPcontacts_ResolvedPeer &result) {
			result.match([&](const MTPDcontacts_resolvedPeer &data) {
				auto &owner = controller()->session().data();
				owner.processUsers(data.vusers());
				owner.processChats(data.vchats());
				const auto peerId = peerFromMTP(data.vpeer());
				const auto peer = peerId
					? owner.peerLoaded(peerId)
					: nullptr;
				const auto user = peer ? peer->asUser() : nullptr;
				if (!user) {
					status->setText(Blacklist::Tr(
						"This username does not belong to a user.",
						"Это имя пользователя принадлежит не пользователю."));
					return;
				} else if (user->isSelf()) {
					status->setText(Blacklist::Tr(
						"You cannot add yourself.",
						"Нельзя добавить самого себя."));
					return;
				}
				const auto added = manager->add(
					peerToUser(user->id),
					user->username().isEmpty()
						? username
						: user->username(),
					user->name());
				if (added) {
					status->setText(Blacklist::Tr(
						"User added to the blacklist.",
						"Пользователь добавлен в чёрный список."));
					field->clear();
				} else {
					status->setText(Blacklist::Tr(
						"This user is already in the list.",
						"Этот пользователь уже есть в списке."));
				}
			});
		})).fail(crl::guard(this, [=] {
			status->setText(Blacklist::Tr(
				"Username not found or the request failed.",
				"Имя пользователя не найдено или запрос не удался."));
		})).send();
	};

	const auto addButton = content->add(
		object_ptr<Ui::SettingsButton>(
			content,
			rpl::single(Blacklist::Tr("Add", "Добавить")),
			st::settingsButtonActive),
		st::boxRowPadding);
	addButton->addClickHandler(submit);
	field->submits(
	) | rpl::on_next([=] {
		submit();
	}, field->lifetime());

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(
		content,
		rpl::single(Blacklist::Tr(
			"Hidden users",
			"Скрытые пользователи")));

	const auto list = content->add(
		object_ptr<Ui::VerticalLayout>(content));

	const auto rebuild = [=] {
		list->clear();
		const auto &entries = manager->entries();
		if (entries.empty()) {
			list->add(
				object_ptr<Ui::FlatLabel>(
					list,
					rpl::single(Blacklist::Tr(
						"No hidden users yet.",
						"Скрытых пользователей пока нет.")),
					st::changePhoneDescription),
				st::boxRowPadding,
				style::al_top);
			return;
		}
		for (const auto &entry : entries) {
			auto text = entry.username.isEmpty()
				? QString::number(entry.id.bare)
				: (u"@"_q + entry.username);
			if (!entry.name.isEmpty()) {
				text += u" — "_q + entry.name;
			}
			const auto button = list->add(
				object_ptr<Ui::SettingsButton>(
					list,
					rpl::single(text),
					st::settingsButtonNoIcon));
			button->setPaddingOverride(style::margins(
				22,
				10,
				48,
				8));
			const auto remove = Ui::CreateChild<Ui::IconButton>(
				button,
				st::sessionTerminate);
			remove->setIconOverride(&st::menuIconDelete);
			remove->setClickedCallback([=, id = entry.id] {
				manager->remove(id);
			});
			const auto reposition = [=] {
				remove->moveToRight(
					4,
					(button->height() - remove->height()) / 2,
					button->width());
			};
			button->widthValue(
			) | rpl::on_next([=](int) {
				reposition();
			}, button->lifetime());
			button->heightValue(
			) | rpl::on_next([=](int) {
				reposition();
			}, button->lifetime());
		}
	};
	rebuild();

	manager->changes(
	) | rpl::on_next([=] {
		crl::on_main(content, [=] {
			rebuild();
			*enabled = manager->enabled();
		});
	}, content->lifetime());

	Ui::ResizeFitChild(this, content);
}

} // namespace

Type BlacklistId() {
	return BlacklistSection::Id();
}

} // namespace Settings

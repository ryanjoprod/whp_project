#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>

#include "window.h"
#include "id_generator.h"

namespace {
	// A starter set of GEDCOM X event types"; "Custom..." takes any URI or word
	constexpr const char* kEventTypes[] = {
		"http://gedcomx.org/Adoption", "http://gedcomx.org/Baptism", "http://gedcomx.org/Birth",
		"http://gedcomx.org/Blessing", "http://gedcomx.org/Burial", "http://gedcomx.org/Census",
		"http://gedcomx.org/Christening", "http://gedcomx.org/Confirmation", "http://gedcomx.org/Cremation",
		"http://gedcomx.org/Death", "http://gedcomx.org/Divorce", "http://gedcomx.org/Education",
		"http://gedcomx.org/Emigration", "http://gedcomx.org/Engagement", "http://gedcomx.org/Funeral",
		"http://gedcomx.org/Immigration", "http://gedcomx.org/LandTransaction", "http://gedcomx.org/Marriage",
		"http://gedcomx.org/MilitaryAward", "http://gedcomx.org/MilitaryDischarge", "http://gedcomx.org/Naturalization",
		"http://gedcomx.org/Ordination", "http://gedcomx.org/Retirement", "Custom..."
	};

	constexpr const char* kEventRoles[] = {
		"http://gedcomx.org/Principal", "http://gedcomx.org/Participant",
		"http://gedcomx.org/Official", "http://gedcomx.org/Witness", "Custom..."
	};

	void copyText(char* dest, size_t size, const std::string& text) {
		dest[0] = '\0';
		strncpy_s(dest, size, text.c_str(), _TRUNCATE);
	}

	std::string lowered(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return s;
	}

	std::string headline(const gedcomx::Event e) {
		std::string text = gedcomx::formatTypeUri(e.type);
		if (!e.date.original.empty()) text += " " + e.date.original;
		if (!e.place.original.empty()) text += ", " + e.place.original;
		if (!e.description.empty()) text += " - " + e.description;
		return text;
	}
}

std::string Window::personEditorRowKey() const {
	return editingExistingPerson_ ? workingPerson_.sourceId + ":" + workingPerson_.id : "";
}

void Window::setPersonEvents(const std::string& personRowKey, std::vector<gedcomx::Event> events, std::unordered_map<std::string, std::string> personNames) {
	personEventsFor_ = personRowKey;
	personEvents_ = std::move(events);
	eventPersonNames_ = std::move(personNames);
}

void Window::setSourceEvents(const std::string& sourceId, std::vector<gedcomx::Event> events, const std::unordered_map<std::string, std::string>& personNames) {
	sourceEventsFor_ = sourceId;
	sourceEvents_ = std::move(events);
	for (const auto& entry : personNames)
		eventPersonNames_[entry.first] = entry.second;  // merged: the Events tab still needs its own
}

void Window::setDetailsEvents(const std::string& personRowKey, std::vector<gedcomx::Event> events, const std::unordered_map<std::string, std::string>& personNames) {
	detailsEventsFor_ = personRowKey;
	detailsEvents_ = std::move(events);
	for (const auto& entry : personNames)
		eventPersonNames_[entry.first] = entry.second;  // shared with the Events tab, so merge
}

std::optional<EventChange> Window::consumeEventChange() {
	auto result = eventChangeResult_;
	eventChangeResult_.reset();
	return result;
}

// --- The Events tab of the Person editor ---

void Window::renderPersonEditorEventsTab() {
	const std::string me = personEditorRowKey();
	if (me.empty()) {
		// This will need to change in the future
		ImGui::TextDisabled("Save the person first, then open them again to add events.");
		return;
	}
	if (personEventsFor_ != me) {  // the answer from App has not arrived (or the load failed: see the log)
		ImGui::TextDisabled("Loading...");
		return;
	}

	if (ImGui::Button("New event...")) openEventEditor(nullptr);
	ImGui::SameLine();
	if (ImGui::Button("Add existing event...")) {
		showExistingEventPicker_ = true;
		existingEventSelected_.clear();
		existingEventFilter_[0] = '\0';
		sourceEventsFor_.clear();
		sourceEvents_.clear();
		pendingSourceEventsRequest_ = workingPerson_.sourceId;
	}
	ImGui::Separator();

	if (personEvents_.empty())
		ImGui::TextDisabled("This person is no in any shared events.");

	auto nameOf = [&](const std::string& personKey) {
		auto it = eventPersonNames_.find(personKey);
		return it != eventPersonNames_.end() && !it->second.empty() ? it->second : std::string("(unnamed)");
	};
	auto roleText = [&](const gedcomx::EventRole& role) {
		std::string text = gedcomx::formatTypeUri(role.type);
		if (!role.details.empty())
			text += " - " + role.details;
		return text;
	};

	for (int i = 0; i < (int)personEvents_.size(); ++i) {
		const gedcomx::Event& e = personEvents_[i];
		ImGui::PushID(i);

		ImGui::BulletText("%s", headline(e).c_str());

		std::string mine, others;
		for (const auto& role : e.roles) {
			if (role.personId == me) {
				mine = roleText(role);
			}
			else {
				if (!others.empty()) others += ", ";
				others += nameOf(role.personId) + " (" + roleText(role) + ")";
			}
		}
		ImGui::TextDisabled("    This person: %s", mine.empty() ? "-" : mine.c_str());
		if (!others.empty())
			ImGui::TextDisabled("    With: %s", others.c_str());

		if (ImGui::SmallButton("Edit...")) openEventEditor(&e);
		ImGui::SameLine();

		const bool onlyOne = e.roles.size() <= 1;
		if (ImGui::SmallButton(onlyOne ? "Delete event" : "Remove from event")) {
			gedcomx::Event changed = e;
			changed.roles.erase(std::remove_if(changed.roles.begin(), changed.roles.end(),
				[&](const gedcomx::EventRole& r) { return r.personId == me; }), changed.roles.end());
			eventChangeResult_ = EventChange{ changed, e.sourceId, me };
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(onlyOne ? "Nobody else is in this event, so it is deleted." : "Takes this person out of the event. The others stay.");

		ImGui::PopID();
		ImGui::Spacing();
	}
}

// --- The Event Editor ---

void Window::openEventEditor(const gedcomx::Event* existing) {
	showEventEditor_ = true;
	eventEditorPersonKey_ = personEditorRowKey();
	workingEvent_ = existing ? *existing : gedcomx::Event();
	workingEvent_.sourceId = workingPerson_.sourceId;

	eventTypeIndex_ = 0;  // a new event starts as the first type
	if (!workingEvent_.type.empty()) {
		eventTypeIndex_ = IM_ARRAYSIZE(kEventTypes) - 1;  // custom unless it matches one
		for (int i = 0; i < IM_ARRAYSIZE(kEventTypes) - 1; ++i)
			if (workingEvent_.type == kEventTypes[i]) eventTypeIndex_ = i;
	}
	copyText(eventTypeCustomBuffer_, sizeof(eventTypeCustomBuffer_), workingEvent_.type);
	copyText(eventDescriptionBuffer_, sizeof(eventDescriptionBuffer_), workingEvent_.description);

	eventDate_ = workingEvent_.date;
	copyText(eventDateBuffer_, sizeof(eventDateBuffer_), workingEvent_.date.original);
	copyText(eventPlaceBuffer_, sizeof(eventPlaceBuffer_), workingEvent_.place.original);
	eventPlaceId_ = workingEvent_.place.descriptionId;

	eventMyRoleIndex_ = -1;
	for (int i = 0; i < (int)workingEvent_.roles.size(); ++i)
		if (workingEvent_.roles[i].personId == eventEditorPersonKey_)
			eventMyRoleIndex_ = i;

	// Joinging an existing event defaults to Participant; starting a new one, to Principal
	std::string roleType = existing ? "http://gedcomx.org/Participant" : "http://gedcomx.org/Principal";
	std::string roleDetails;
	if (eventMyRoleIndex_ >= 0) {
		roleType = workingEvent_.roles[eventMyRoleIndex_].type;
		roleDetails = workingEvent_.roles[eventMyRoleIndex_].details;
	}
	eventRoleIndex_ = IM_ARRAYSIZE(kEventRoles) - 1;
	for (int i = 0; i < IM_ARRAYSIZE(kEventRoles) - 1; ++i)
		if (roleType == kEventRoles[i]) eventRoleIndex_ = i;
	copyText(eventRoleCustomBuffer_, sizeof(eventRoleCustomBuffer_), roleType);
	copyText(eventRoleDetailsBuffer_, sizeof(eventRoleDetailsBuffer_), roleDetails);

	fillEditableNotes(eventNotes_, workingEvent_.conclusion.notes);
}

std::string Window::collectEvent(gedcomx::Event& out) const {
	out = workingEvent_;  // keeps the id, other people's roles, confidence and language
	const bool customType = eventTypeIndex_ == IM_ARRAYSIZE(kEventTypes) - 1;
	out.type = customType ? eventTypeCustomBuffer_ : kEventTypes[eventTypeIndex_];
	out.description = eventDescriptionBuffer_;
	out.date = eventDate_;
	out.place.original = eventPlaceBuffer_;
	out.place.descriptionId = eventPlaceId_;
	out.conclusion.notes = collectNotes(eventNotes_);

	const bool customRole = eventRoleIndex_ == IM_ARRAYSIZE(kEventRoles) - 1;
	gedcomx::EventRole role = eventMyRoleIndex_ >= 0 ? workingEvent_.roles[eventMyRoleIndex_] : gedcomx::EventRole{};  // keeps its notes and citations
	role.personId = eventEditorPersonKey_;
	role.type = customRole ? eventRoleCustomBuffer_ : kEventRoles[eventRoleIndex_];
	role.details = eventRoleDetailsBuffer_;
	if (eventMyRoleIndex_ >= 0)
		out.roles[eventMyRoleIndex_] = role;
	else
		out.roles.push_back(role);

	if (out.type.empty()) return "Enter the event type.";
	if (role.type.empty()) return "Enter the role.";
	return "";
}

void Window::renderEventEditor(const std::vector<gedcomx::SourceDescription>& knownSources) {
	if (showEventEditor_ && !ImGui::IsPopupOpen("Event Editor")) {
		ImGui::OpenPopup("Event Editor");
	}

	if (beginModal("Event Editor", &showEventEditor_, ImGuiWindowFlags_AlwaysAutoResize)) {
		gedcomx::Event preview;
		const std::string error = collectEvent(preview);

		int others = 0;
		for (const auto& role : workingEvent_.roles)
			if (role.personId != eventEditorPersonKey_) ++others;
		if (others > 0)
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Shared with %d oter %s: changes to the event apply to all of them.", others, others == 1 ? "person" : "people");

		ImGui::SetNextItemWidth(260.0f);
		if (ImGui::BeginCombo("Type", gedcomx::formatTypeUri(kEventTypes[eventTypeIndex_]).c_str())) {
			for (int i = 0; i < IM_ARRAYSIZE(kEventTypes); ++i)
				if (ImGui::Selectable(gedcomx::formatTypeUri(kEventTypes[i]).c_str(), i == eventTypeIndex_)) eventTypeIndex_ = i;
			ImGui::EndCombo();
		}
		if (eventTypeIndex_ == IM_ARRAYSIZE(kEventTypes) - 1) {
			ImGui::SetNextItemWidth(260.0f);
			ImGui::InputText("Custom type URI", eventTypeCustomBuffer_, sizeof(eventTypeCustomBuffer_));
		}

		if (renderDateInput("eventDate", eventDate_, eventDateBuffer_, sizeof(eventDateBuffer_), 260.0f))
			openDateEditor(DateEditorTarget::Event, eventDate_);
		ImGui::SameLine();
		ImGui::TextUnformatted("Date");

		ImGui::SetNextItemWidth(260.0f);
		ImGui::InputText("Place", eventPlaceBuffer_, sizeof(eventPlaceBuffer_));  // what the source says
		if (renderPlacePicker("#eventPlace", eventPlaceId_, nullptr, 200.0f)) {
			if (eventPlaceBuffer_[0] == '\0')
				copyText(eventPlaceBuffer_, sizeof(eventPlaceBuffer_), placeTitleById(eventPlaceId_));
		}
		ImGui::SameLine(0.0f, 4.0f);
		ImGui::BeginDisabled(eventPlaceId_.empty());
		if (ImGui::SmallButton("Unlink")) eventPlaceId_.clear();
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextUnformatted("Linked place");

		ImGui::SetNextItemWidth(260.0f);
		if (ImGui::BeginCombo("Role", gedcomx::formatTypeUri(kEventRoles[eventRoleIndex_]).c_str())) {
			for (int i = 0; i < IM_ARRAYSIZE(kEventRoles); ++i)
				if (ImGui::Selectable(gedcomx::formatTypeUri(kEventRoles[i]).c_str(), i == eventRoleIndex_)) eventRoleIndex_ = i;
			ImGui::EndCombo();
		}
		if (eventRoleIndex_ == IM_ARRAYSIZE(kEventRoles) - 1) {
			ImGui::SetNextItemWidth(260.0f);
			ImGui::InputText("Custom role URI", eventRoleCustomBuffer_, sizeof(eventRoleCustomBuffer_));
		}
		ImGui::SetNextItemWidth(260.0f);
		ImGui::InputText("Role details", eventRoleDetailsBuffer_, sizeof(eventRoleDetailsBuffer_));
		ImGui::LabelText("ID", "%s", workingEvent_.id.empty() ? "(assigned on OK)" : workingEvent_.id.c_str());

		if (ImGui::BeginTabBar("EventEditorTabs")) {
			auto tab = [&](const char* label, auto&& body) {
				if (ImGui::BeginTabItem(label)) {
					ImGui::BeginChild(label, ImVec2(520.0f, 150.0f));  // fixed size: the popup must not resize per tab
					body();
					ImGui::EndChild();
					ImGui::EndTabItem();
				}
			};
			tab("People", [&] {
				int shown = 0;
				for (const auto& role : workingEvent_.roles) {
					if (role.personId == eventEditorPersonKey_) continue;
					auto it = eventPersonNames_.find(role.personId);
					ImGui::BulletText("%s - %s", it != eventPersonNames_.end() ? it->second.c_str() : "(unnamed)", gedcomx::formatTypeUri(role.type).c_str());
					++shown;
				}
				if (shown == 0)
					ImGui::TextDisabled("Nobody else is in this event yet. Add people from their own editorw with \"Add existing event...\".");
			});
			tab("Notes", [&] { renderEditableNotes(eventNotes_); });
			tab("Citations", [&] { renderCitationRefs(CitationEditorOwner::Event, workingEvent_.conclusion.sources, knownSources); });
			ImGui::EndTabBar();
		}

		ImGui::Separator();
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", error.empty() ? " " : error.c_str());

		ImGui::BeginDisabled(!error.empty());
		if (ImGui::Button("OK")) {
			gedcomx::Event e = preview;
			if (e.id.empty()) e.id = idgen::generateLocalId("E_");
			eventChangeResult_ = EventChange{ e, e.sourceId, eventEditorPersonKey_ };
			showEventEditor_ = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			showEventEditor_ = false;
			ImGui::CloseCurrentPopup();
		}

		if (auto picked = renderDateEditor(DateEditorTarget::Event)) {
			eventDate_ = *picked;
			copyText(eventDateBuffer_, sizeof(eventDateBuffer_), picked->original);
		}
		renderCitationEditor(CitationEditorOwner::Event, knownSources);

		ImGui::EndPopup();
	}
}

// --- Add Existing Event ---

void Window::renderExistinEventPicker() {
	if (showExistingEventPicker_ && !ImGui::IsPopupOpen("Add Existing Event")) {
		ImGui::OpenPopup("Add Existing Event");
	}

	ImGui::SetNextWindowSize(ImVec2(520.0f, 360.0f), ImGuiCond_Appearing);
	if (beginModal("Add Existing Event", &showExistingEventPicker_)) {
		const std::string me = personEditorRowKey();
		const gedcomx::Event* selected = nullptr;

		if (sourceEventsFor_ != workingPerson_.sourceId) {
			ImGui::TextDisabled("Loading...");
		}
		else {
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##eventFilter", "Filter...", existingEventFilter_, sizeof(existingEventFilter_));
			const std::string needle = lowered(existingEventFilter_);

			if (ImGui::BeginChild("##existingEvents", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)))) {
				for (const auto& e : sourceEvents_) {
					const bool alreadyIn = std::any_of(e.roles.begin(), e.roles.end(),
						[&](const gedcomx::EventRole& r) { return r.personId == me; });
					if (alreadyIn) continue;

					const std::string label = headline(e) + " (" + std::to_string(e.roles.size()) + " people)";
					if (!needle.empty() && lowered(label).find(needle) == std::string::npos) continue;

					ImGui::PushID(e.id.c_str());
					if (ImGui::Selectable(label.c_str(), e.id == existingEventSelected_))
						existingEventSelected_ = e.id;
					ImGui::PopID();
				}
			}
			ImGui::EndChild();

			for (const auto& e : sourceEvents_)
				if (e.id == existingEventSelected_) selected = &e;
		}

		ImGui::BeginDisabled(selected == nullptr);
		if (ImGui::Button("Add to this event...")) {
			const gedcomx::Event chosen = *selected;  // a copy: the list may change under us
			showExistingEventPicker_ = false;
			ImGui::CloseCurrentPopup();
			openEventEditor(&chosen);
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			showExistingEventPicker_ = false;
			ImGui::CloseCurrentPopup();
		}							   
		ImGui::EndPopup();
	}
}
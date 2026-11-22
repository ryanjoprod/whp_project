#include <cstring>

#include "window.h"

namespace {
	const char* const kDateCalendars[] = { "Gregorian", "Julian", "Hebrew", "French Republican", "Persian", "Islamic", "Swedish" };
	const char* const kDateKinds[] = { "Regular", "Before", "After", "About", "Between ... and ...", "From ... to ...", "Text only" };  // same order as gedcomx::DateKind
	const char* const kDateQualities[] = { "Regular", "Estimated", "Calculated" };
	const char* const kDateQualityValues[] = { "", "estimated", "calculated" };
	const char* const kDateMonths[] = { "(none)", "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };

	void dateFields(const char* label, const char* id, gedcomx::SimpleDate& d, bool namedMonths) {
		ImGui::PushID(id);
		ImGui::TextUnformatted(label);

		ImGui::SetNextItemWidth(70.0f);
		ImGui::InputInt("Day", &d.day, 0, 0);
		ImGui::SameLine();
		if (namedMonths) {
			ImGui::SetNextItemWidth(130.0f);
			ImGui::Combo("Month", &d.month, kDateMonths, IM_ARRAYSIZE(kDateMonths));  // 0 = none, 1..12
		}
		else {
			ImGui::SetNextItemWidth(70.0f);
			ImGui::InputInt("Month", &d.month, 0, 0);
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::InputInt("Year", &d.year, 0, 0);
		ImGui::PopID();
	}
}

void Window::openDateEditor(DateEditorTarget target, const gedcomx::Date& initial) {
	const gedcomx::DateParts parts = gedcomx::fromDate(initial);

	dateEditorTarget_ = target;
	showDateEditor_ = true;

	dateKindIndex_ = (int)parts.kind;
	dateStart_ = parts.start;
	dateStop_ = parts.stop;

	dateCalendarIndex_ = 0;
	for (int i = 0; i < IM_ARRAYSIZE(kDateCalendars); ++i)
		if (parts.calendar == kDateCalendars[i])
			dateCalendarIndex_ = i;

	dateQualityIndex_ = 0;
	for (int i = 0; i < IM_ARRAYSIZE(kDateQualityValues); ++i)
		if (parts.quality == kDateQualityValues[i])
			dateQualityIndex_ = i;

	dateTextBuffer_[0] = '\0';
	strncpy_s(dateTextBuffer_, parts.text.c_str(), sizeof(dateTextBuffer_) - 1);
}

gedcomx::DateParts Window::dateEditorParts() const {
	gedcomx::DateParts p;
	p.kind = (gedcomx::DateKind)dateKindIndex_;
	p.calendar = dateCalendarIndex_ == 0 ? "" : kDateCalendars[dateCalendarIndex_];
	p.quality = kDateQualityValues[dateQualityIndex_];
	p.start = dateStart_;
	p.stop = dateStop_;
	p.text = dateTextBuffer_;
	return p;
}

std::optional<gedcomx::Date> Window::renderDateEditor(DateEditorTarget target) {
	std::optional<gedcomx::Date> result;
	if (dateEditorTarget_ != target) return result;  // only the editor that asked for it renders it

	if (showDateEditor_ && !ImGui::IsPopupOpen("Date")) {
		ImGui::OpenPopup("Date");
	}

	if (ImGui::BeginPopupModal("Date", &showDateEditor_, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::SetNextItemWidth(220.0f);
		ImGui::Combo("Type", &dateKindIndex_, kDateKinds, IM_ARRAYSIZE(kDateKinds));
		ImGui::SetNextItemWidth(220.0f);
		ImGui::Combo("Quality", &dateQualityIndex_, kDateQualities, IM_ARRAYSIZE(kDateQualities));
		ImGui::SetNextItemWidth(220.0f);
		ImGui::Combo("Calendar", &dateCalendarIndex_, kDateCalendars, IM_ARRAYSIZE(kDateCalendars));

		const auto kind = (gedcomx::DateKind)dateKindIndex_;
		const bool textOnly = kind == gedcomx::DateKind::TextOnly;
		const bool twoDates = kind == gedcomx::DateKind::Range || kind == gedcomx::DateKind::Span;
		const bool namedMonths = dateCalendarIndex_ <= 1;  // Gregorian and Julian

		ImGui::Separator();
		if (textOnly) {
			ImGui::SetNextItemWidth(320.0f);
			ImGui::InputText("Text", dateTextBuffer_, sizeof(dateTextBuffer_));
		}
		else {
			dateFields(twoDates ? "Start" : "Date", "start", dateStart_, namedMonths);
			if (twoDates) dateFields("End", "stop", dateStop_, namedMonths);
			ImGui::TextDisabled("Leave the day or month at 0 if unknown. A negative year is BC.");
		}

		const gedcomx::DateParts parts = dateEditorParts();
		const std::string error = gedcomx::validateDateParts(parts);

		ImGui::Separator();
		if (!error.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", error.c_str());
		}
		else {
			const gedcomx::Date preview = gedcomx::toDate(parts);
			ImGui::LabelText("Reads as", "%s", preview.original.empty() ? "(no date)" : preview.original.c_str());
			ImGui::LabelText("Formal", "%s", preview.formal.empty() ? "-" : preview.formal.c_str());
			ImGui::LabelText("Sort value", "%s", preview.sortValue ? std::to_string(*preview.sortValue).c_str() : "unknown");
		}

		ImGui::Separator();
		ImGui::BeginDisabled(!error.empty());
		if (ImGui::Button("OK")) {
			result = gedcomx::toDate(parts);
			showDateEditor_ = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			showDateEditor_ = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (!showDateEditor_) dateEditorTarget_ = DateEditorTarget::None;
	return result;
}
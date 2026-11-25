#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>

#include "window.h"
#include "id_generator.h"

namespace {
	constexpr const char* kPlaceTypes[] = { "Country", "State", "Province", "County", "Township", "Parish", "District", "Cemetary", "Church", "Building", "Other" };

	std::string lowered(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(),
			[](unsigned char c) {
				return (char)std::tolower(c);
		});
		return s;
	}

	void copyText(char* dest, size_t size, const std::string& text) {
		dest[0] = '\0';
		strncpy_s(dest, size, text.c_str(), _TRUNCATE);
	}
}

// --- Known places (pushed by App) ---

void Window::setKnownPlaces(const std::vector<gedcomx::PlaceDescription>& places) {
	knownPlaces_ = places;
	placeIndex_ = gedcomx::indexPlaces(knownPlaces_);

	placePickList_.clear();
	for (const auto& p : knownPlaces_)
		placePickList_.emplace_back(gedcomx::placeTitle(p, placeIndex_), p.id);
	std::sort(placePickList_.begin(), placePickList_.end(),
		[](const auto& a, const auto& b) {
			return lowered(a.first) < lowered(b.first);
	});
}

std::optional<gedcomx::PlaceDescription> Window::consumePlaceSave() {
	auto result = placeSaveResult_;
	placeSaveResult_.reset();
	return result;
}

std::string Window::placeTitleById(const std::string& id) const {
	auto it = placeIndex_.find(id);
	return it == placeIndex_.end() ? id + " (missing)" : gedcomx::placeTitle(*it->second, placeIndex_);
}

// --- Open and collect ---

void Window::openPlaceEditor(PlaceEditorOwner owner, const gedcomx::PlaceDescription* existing) {
	placeEditorOwner_ = owner;
	showPlaceEditor_ = true;
	workingPlace_ = existing ? *existing : gedcomx::PlaceDescription{};

	copyText(placeTypeBuffer_, sizeof(placeTypeBuffer_), workingPlace_.type);
	copyText(placeCodeBuffer_, sizeof(placeCodeBuffer_), workingPlace_.code);
	copyText(placeLatBuffer_, sizeof(placeLatBuffer_), workingPlace_.latitude ? gedcomx::formatCoordinate(*workingPlace_.latitude) : "");
	copyText(placeLonBuffer_, sizeof(placeLonBuffer_), workingPlace_.longitude ? gedcomx::formatCoordinate(*workingPlace_.longitude) : "");

	placeNames_.clear();
	for (const auto& n : workingPlace_.names) {
		EditablePlaceName row;
		copyText(row.value, sizeof(row.value), n.value);
		copyText(row.lang, sizeof(row.lang), n.lang);
		row.date = n.date;
		copyText(row.dateText, sizeof(row.dateText), n.date.original);
		placeNames_.push_back(row);
	}
	if (placeNames_.empty()) placeNames_.emplace_back();  // a new place starts with one empty name

	placeParents_.clear();
	for (const auto& j : workingPlace_.jurisdictions) {
		EditablePlaceParent row;
		row.parentId = j.parentId;
		row.date = j.date;
		copyText(row.dateText, sizeof(row.dateText), j.date.original);
		placeParents_.push_back(row);
	}

	placeUrls_.clear();
	for (const auto& u : workingPlace_.urls) {
		EditablePlaceUrl row;
		copyText(row.url, sizeof(row.url), u.url);
		copyText(row.description, sizeof(row.description), u.description);
		copyText(row.type, sizeof(row.type), u.type);
		placeUrls_.push_back(row);
	}

	fillEditableNotes(placeNotes_, workingPlace_.conclusion.notes);

	placeParentExcluded_.clear();
	if (!workingPlace_.id.empty()) {
		placeParentExcluded_ = gedcomx::placeDescendants(workingPlace_.id, knownPlaces_);
		placeParentExcluded_.insert(workingPlace_.id);
	}
	placeParentFilterBuffer_[0] = '\0';
	placeDateRow_ = -1;
}

std::string Window::collectPlace(gedcomx::PlaceDescription& out) const {
	out = workingPlace_;  // keeps the id, confidence, language and citations
	out.type = placeTypeBuffer_;
	out.code = placeCodeBuffer_;

	if (std::string e = gedcomx::parseCoordinate(placeLatBuffer_, 90.0, out.latitude); !e.empty()) return "Latitude: " + e;
	if (std::string e = gedcomx::parseCoordinate(placeLonBuffer_, 180.0, out.longitude); !e.empty()) return "Longitude: " + e;

	out.names.clear();
	for (const auto& row : placeNames_) {
		if (row.value[0] == '\0') continue;  // a blank row is not a name
		out.names.push_back({ row.lang, row.value, row.date });
	}

	out.jurisdictions.clear();
	for (const auto& row : placeParents_)
		out.jurisdictions.push_back({ row.parentId, row.date });

	out.urls.clear();
	for (const auto& row : placeUrls_) {
		if (row.url[0] == '\0' && row.description[0] == '\0' && row.type[0] == '\0') continue;
		out.urls.push_back({ row.url, row.description, row.type });
	}

	out.conclusion.notes = collectNotes(placeNotes_);
	return gedcomx::validatePlace(out);
}

// A combo that picks a place by title, with a filter. Returns true on the frame a place is picked
bool Window::renderPlacePicker(const char* id, std::string& placeId, const std::unordered_set<std::string>* excluded, float width) {
	bool picked = false;
	const std::string preview = placeId.empty() ? "Choose a place..." : placeTitleById(placeId);

	ImGui::SetNextItemWidth(width);
	if (ImGui::BeginCombo(id, preview.c_str())) {
		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		ImGui::InputTextWithHint("##filter", "Filter...", placeParentFilterBuffer_, sizeof(placeParentFilterBuffer_));  // one filter box shared by every picker
		const std::string needle = lowered(placeParentFilterBuffer_);

		for (const auto& entry : placePickList_) {
			if (excluded && excluded->count(entry.second)) continue;
			if (!needle.empty() && lowered(entry.first).find(needle) == std::string::npos) continue;
			ImGui::PushID(entry.second.c_str());
			if (ImGui::Selectable(entry.first.c_str(), entry.second == placeId)) {
				placeId = entry.second;
				placeParentFilterBuffer_[0] = '\0';
				picked = true;
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	return picked;
}

// --- Tabs ---

void Window::renderPlaceNamesTab() {
	for (int i = 0; i < (int)placeNames_.size(); ++i) {
		auto& row = placeNames_[i];
		ImGui::PushID(i);
		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputTextWithHint("##value", i == 0 ? "Primary name" : "Other name", row.value, sizeof(row.value));
		ImGui::SameLine(0.0f, 4.0f);
		ImGui::InputTextWithHint("##lang", "lang", row.lang, sizeof(row.lang));
		ImGui::SameLine(0.0f, 4.0f);
		if (renderDateInput("date", row.date, row.dateText, sizeof(row.dateText), 150.0f)) {
			placeDateRow_ = i;
			openDateEditor(DateEditorTarget::PlaceName, row.date);
		}
		ImGui::SameLine();
		const bool removed = ImGui::SmallButton("Remove");
		ImGui::PopID();
		if (removed) {
			placeNames_.erase(placeNames_.begin() + i);
			break;
		}
	}
	if (ImGui::Button("Add name")) placeNames_.emplace_back();
	ImGui::TextDisabled("The first name is the primary one. A date says when a name was in use.");
}

void Window::renderPlaceLocationTab() {
	ImGui::SetNextItemWidth(200.0f);
	ImGui::InputText("##type", placeTypeBuffer_, sizeof(placeTypeBuffer_));
	ImGui::SameLine(0.0f, 4.0f);
	if (ImGui::BeginCombo("##typePick", "", ImGuiComboFlags_NoPreview)) {
		for (const char* t : kPlaceTypes)
			if (ImGui::Selectable(t))
				copyText(placeTypeBuffer_, sizeof(placeTypeBuffer_), t);
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::TextUnformatted("Type");
	
	ImGui::SetNextItemWidth(200.0f);
	ImGui::InputText("Code", placeCodeBuffer_, sizeof(placeCodeBuffer_));

	std::optional<double> unused;
	const std::string latError = gedcomx::parseCoordinate(placeLatBuffer_, 90.0, unused);
	const std::string lonError = gedcomx::parseCoordinate(placeLonBuffer_, 180.0, unused);

	ImGui::SetNextItemWidth(200.0f);
	ImGui::InputText("Latitude", placeLatBuffer_, sizeof(placeLatBuffer_));
	if (!latError.empty())
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", latError.c_str());
	ImGui::SetNextItemWidth(200.0f);
	ImGui::InputText("Longitude", placeLonBuffer_, sizeof(placeLonBuffer_));
	if (!lonError.empty())
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", lonError.c_str());
	ImGui::TextDisabled("Decimal degrees, like 38.8462 and -77.3064. Leave blank if unknown.");

	ImGui::LabelText("ID", "%s", workingPlace_.id.empty() ? "(assigned on OK)" : workingPlace_.id.c_str());
}

void Window::renderPlaceParentsTab() {
	for (int i = 0; i < (int)placeParents_.size(); ++i) {
		auto& row = placeParents_[i];
		ImGui::PushID(i);

		renderPlacePicker("##parent", row.parentId, &placeParentExcluded_, 300.0f);

		ImGui::SameLine(0.0f, 4.0f);
		if (renderDateInput("date", row.date, row.dateText, sizeof(row.dateText), 150.0f)) {
			placeDateRow_ = i;
			openDateEditor(DateEditorTarget::PlaceParent, row.date);
		}
		ImGui::SameLine();
		const bool removed = ImGui::SmallButton("Remove");
		ImGui::PopID();
		if (removed) {
			placeParents_.erase(placeParents_.begin() + i);
			break;
		}
	}
	if (ImGui::Button("Add parent"))
		placeParents_.emplace_back();
	ImGui::TextDisabled("The first parent is the main one: it builds the place's title.");
	ImGui::TextDisabled("Add more, with dates, if the place moved between parents.");
}

void Window::renderPlaceLinksTab() {
	for (int i = 0; i < (int)placeUrls_.size(); ++i) {
		auto& row = placeUrls_[i];
		ImGui::PushID(i);
		ImGui::SetNextItemWidth(250.0f);
		ImGui::InputTextWithHint("##url", "https://...", row.url, sizeof(row.url));
		ImGui::SameLine(0.0f, 4.0f);
		ImGui::SetNextItemWidth(130.0f);
		ImGui::InputTextWithHint("##description", "description", row.description, sizeof(row.description));
		ImGui::SameLine(0.0f, 4.0f);
		ImGui::SetNextItemWidth(70.0f);
		ImGui::InputTextWithHint("##type", "type", row.type, sizeof(row.type));
		ImGui::SameLine();
		const bool removed = ImGui::SmallButton("Remove");
		ImGui::PopID();
		if (removed) {
			placeUrls_.erase(placeUrls_.begin() + i);
			break;
		}
	}
	if (ImGui::Button("Add link")) placeUrls_.emplace_back();
}

void Window::renderPlaceCitationsTab(const std::vector<gedcomx::SourceDescription>& knownSources) {
	renderCitationRefs(CitationEditorOwner::Place, workingPlace_.conclusion.sources, knownSources);
}

// --- The Place Editor ---

void Window::renderPlaceEditor(PlaceEditorOwner owner, const std::vector<gedcomx::SourceDescription>& knownSources) {
	if (placeEditorOwner_ != owner) return;  // only the place that asked for it renders it

	if (showPlaceEditor_ && !ImGui::IsPopupOpen("Place Editor")) {
		ImGui::OpenPopup("Place Editor");
	}

	if (beginModal("Place Editor", &showPlaceEditor_, ImGuiWindowFlags_AlwaysAutoResize)) {
		gedcomx::PlaceDescription preview;
		const std::string error = collectPlace(preview);

		ImGui::TextUnformatted(gedcomx::placeTitle(preview, placeIndex_).c_str());
		ImGui::Separator();

		if (ImGui::BeginTabBar("PlaceEditorTab")) {
			auto tab = [&](const char* label, auto&& body) {
				if (ImGui::BeginTabItem(label)) {
					ImGui::BeginChild(label, ImVec2(560.0f, 240.0f));  // fixed size: the popup must not resize per tab
					body();
					ImGui::EndChild();
					ImGui::EndTabItem();
				}
				};
			tab("Names", [&] { renderPlaceNamesTab(); });
			tab("Location", [&] { renderPlaceLocationTab(); });
			tab("Parents", [&] { renderPlaceParentsTab(); });
			tab("Links", [&] { renderPlaceLinksTab(); });
			tab("Citations", [&] { renderPlaceCitationsTab(knownSources); });
			tab("Notes", [&] { renderEditableNotes(placeNotes_); });
			ImGui::EndTabBar();
		}

		ImGui::Separator();
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", error.empty() ? " " : error.c_str());

		ImGui::BeginDisabled(!error.empty());
		if (ImGui::Button("OK")) {
			gedcomx::PlaceDescription p = preview;
			if (p.id.empty())
				p.id = idgen::generateLocalId("PL_");
			placeSaveResult_ = p;
			showPlaceEditor_ = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			showPlaceEditor_ = false;
			ImGui::CloseCurrentPopup();
		}

		// The Date editor renders inside this popup, like it does inside the Citation Editor
		if (auto picked = renderDateEditor(DateEditorTarget::PlaceName)) {
			if (placeDateRow_ >= 0 && placeDateRow_ < (int)placeNames_.size()) {
				auto& row = placeNames_[placeDateRow_];
				row.date = *picked;
				copyText(row.dateText, sizeof(row.dateText), picked->original);
			}
		}
		if (auto picked = renderDateEditor(DateEditorTarget::PlaceParent)) {
			if (placeDateRow_ >= 0 && placeDateRow_ < (int)placeParents_.size()) {
				auto& row = placeParents_[placeDateRow_];
				row.date = *picked;
				copyText(row.dateText, sizeof(row.dateText), picked->original);
			}
		}

		renderCitationEditor(CitationEditorOwner::Place, knownSources);

		ImGui::EndPopup();
	}

	if (!showPlaceEditor_) placeEditorOwner_ = PlaceEditorOwner::None;
}

// --- The Places List ---

void Window::renderPlaceList(const std::vector<gedcomx::SourceDescription>& knownSources) {
	if (showPlaceList_ && !ImGui::IsPopupOpen("Places")) {
		ImGui::OpenPopup("Places");
	}

	ImGui::SetNextWindowSize(ImVec2(520.0f, 440.0f), ImGuiCond_Appearing);
	if (beginModal("Places", &showPlaceList_)) {
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##placeFilter", "Filter...", placeFilterBuffer_, sizeof(placeFilterBuffer_));
		const std::string needle = lowered(placeFilterBuffer_);

		auto editSelected = [&] {
			auto it = placeIndex_.find(selectedPlaceId_);
			if (it != placeIndex_.end())
				openPlaceEditor(PlaceEditorOwner::List, it->second);
		};

		if (ImGui::BeginChild("##placeRows", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + 4.0f)))) {
			for (const auto& entry : placePickList_) {
				if (!needle.empty() && lowered(entry.first).find(needle) == std::string::npos) continue;
				ImGui::PushID(entry.second.c_str());
				if (ImGui::Selectable(entry.first.c_str(), entry.second == selectedPlaceId_, ImGuiSelectableFlags_AllowDoubleClick)) {
					selectedPlaceId_ = entry.second;
					if (ImGui::IsMouseDoubleClicked(0)) editSelected();
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();

		if (ImGui::Button("New place..."))
			openPlaceEditor(PlaceEditorOwner::List, nullptr);
		ImGui::SameLine();
		ImGui::BeginDisabled(placeIndex_.find(selectedPlaceId_) == placeIndex_.end());
		if (ImGui::Button("Edit...")) editSelected();
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Close")) {
			showPlaceList_ = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%d places", (int)placePickList_.size());

		renderPlaceEditor(PlaceEditorOwner::List, knownSources);  // inside ths popup, so the two modals nest
		ImGui::EndPopup();
	}
}
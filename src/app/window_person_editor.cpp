#include <algorithm>

#include "window.h"
#include "id_generator.h"

namespace {
	const char* kFactTypes[] = {
		"http://gedcomx.org/Birth",
		"http://gedcomx.org/Death",
		"http://gedcomx.org/Marriage",
		"http://gedcomx.org/Residence",
		"Custom..."
	};
	const char* kGenderTypes[] = {
		"http://gedcomx.org/Female",
		"http://gedcomx.org/Male",
		"http://gedcomx.org/Unknown",
		"http://gedcomx.org/Other"
	};
}

void Window::requestNewPersonEditor() {
	workingPerson_ = gedcomx::Person{};
	editingExistingPerson_ = false;
	editNames_.clear();

	primaryGivenBuffer_[0] = primarySurnameBuffer_[0] = primaryTitleBuffer_[0] = primaryNicknameBuffer_[0] = primaryCallNameBuffer_[0] = '\0';
	genderIndex_ = 2;

	selectedSourceIndex_ = -1;
	editingFactIndex_ = -1;
	factDateBuffer_[0] = factPlaceBuffer_[0] = factTypeCustomBuffer_[0] = '\0';
	selectedFactTypeIndex_ = 0;

	editingAttributeIndex_ = -1;
	attrTypeBuffer_[0] = attrValueBuffer_[0] = '\0';

	editingAddressIndex_ = -1;
	addrStateBuffer_[0] = addrCityBuffer_[0] = addrStateBuffer_[0] = addrPostalBuffer_[0] = addrCountryBuffer_[0] = '\0';

	showPersonEditor_ = true;
}

void Window::requestEditPersonEditor(const gedcomx::Person& person) {
	workingPerson_ = person;
	workingPerson_.id = storage::stripRowKeyPrefix(person.id, person.sourceId);
	editingExistingPerson_ = true;

	primaryGivenBuffer_[0] = primarySurnameBuffer_[0] = primaryTitleBuffer_[0] = primaryNicknameBuffer_[0] = primaryCallNameBuffer_[0] = '\0';
	editNames_.clear();

	if (!person.names.empty()) {
		const auto& primary = person.names[0];
		strncpy_s(primaryTitleBuffer_, primary.title.c_str(), sizeof(primaryTitleBuffer_) - 1);
		strncpy_s(primaryNicknameBuffer_, primary.nickName.c_str(), sizeof(primaryNicknameBuffer_) - 1);
		strncpy_s(primaryCallNameBuffer_, primary.callName.c_str(), sizeof(primaryCallNameBuffer_) - 1);

		if (!primary.nameForms.empty()) {
			for (const auto& part : primary.nameForms[0].parts) {
				if (part.type == "http://gedcomx.org/Given")
					strncpy_s(primaryGivenBuffer_, part.value.c_str(), sizeof(primaryGivenBuffer_) - 1);
				else if (part.type == "http://gedcomx.org/Surname")
					strncpy_s(primarySurnameBuffer_, part.value.c_str(), sizeof(primarySurnameBuffer_) - 1);
			}
		}

		// Anything past the primary name is an alternate - Names tab only.
		for (size_t i = 1; i < person.names.size(); ++i) {
			EditableNameEntry entry;
			if (!person.names[i].nameForms.empty()) {
				for (const auto& part : person.names[i].nameForms[0].parts) {
					if (part.type == "http://gedcomx.org/Given")
						strncpy_s(entry.given, part.value.c_str(), sizeof(entry.given) - 1);
					else if (part.type == "http://gedcomx.org/Surname")
						strncpy_s(entry.surname, part.value.c_str(), sizeof(entry.surname) - 1);
				}
			}
			editNames_.push_back(entry);
		}
	}

	genderIndex_ = 2;
	if (person.gender) {
		for (int i = 0; i < IM_ARRAYSIZE(kGenderTypes); ++i) {
			if (person.gender->type == kGenderTypes[i]) {
				genderIndex_ = i;
				break;
			}
		}
	}

	selectedSourceIndex_ = -1;  // resolved against knownSources in the top strip
	editingFactIndex_ = -1;
	factDateBuffer_[0] = factPlaceBuffer_[0] = factTypeCustomBuffer_[0] = '\0';
	selectedFactTypeIndex_ = 0;
	selectedSourceToAddIndex_ = -1;

	editingAttributeIndex_ = -1;
	attrTypeBuffer_[0] = attrValueBuffer_[0] = '\0';

	editingAddressIndex_ = -1;
	addrStreetBuffer_[0] = addrCityBuffer_[0] = addrStateBuffer_[0] = addrPostalBuffer_[0] = addrCountryBuffer_[0] = '\0';

	showPersonEditor_ = true;
}

void Window::renderPersonEditorTopStrip(const std::vector<gedcomx::SourceDescription>& knownSources) {
	ImGui::InputText("Given", primaryGivenBuffer_, sizeof(primaryGivenBuffer_));
	ImGui::InputText("Surname", primarySurnameBuffer_, sizeof(primarySurnameBuffer_));
	ImGui::InputText("Title", primaryTitleBuffer_, sizeof(primaryTitleBuffer_));
	ImGui::InputText("Nickname", primaryNicknameBuffer_, sizeof(primaryNicknameBuffer_));
	ImGui::InputText("Call Name", primaryCallNameBuffer_, sizeof(primaryCallNameBuffer_));
	if (ImGui::BeginCombo("Gender", genderIndex_ >= 0 && genderIndex_ < IM_ARRAYSIZE(kGenderTypes)
		? gedcomx::formatTypeUri(kGenderTypes[genderIndex_]).c_str() : "")) {
		for (int i = 0; i < IM_ARRAYSIZE(kGenderTypes); ++i) {
			std::string label = gedcomx::formatTypeUri(kGenderTypes[i]);
			if (ImGui::Selectable(label.c_str(), i == genderIndex_)) genderIndex_ = 1;
		}
		ImGui::EndCombo();
	}

	ImGui::Spacing();
	ImGui::Text("Source");

	if (selectedSourceIndex_ < 0 && !workingPerson_.sourceId.empty()) {
		for (int i = 0; i < (int)knownSources.size(); ++i) {
			if (knownSources[i].id == workingPerson_.sourceId) {
				selectedSourceIndex_ = i;
				break;
			}
		}
	}

	ImGui::BeginDisabled(editingExistingPerson_);
	if (ImGui::BeginCombo("##EditSourceCombo",
		selectedSourceIndex_ >= 0 && selectedSourceIndex_ < (int)knownSources.size()
		? knownSources[selectedSourceIndex_].primaryTitle().c_str() : "Select a source...")) {
		renderSelectableList<gedcomx::SourceDescription>(
			knownSources, &selectedSourceIndex_,
			[](const gedcomx::SourceDescription& s) { return s.primaryTitle(); },
			[](const gedcomx::SourceDescription& s) { return s.id; }
		);
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("+ New Source")) {
		ImGui::OpenPopup("New Source");
	}
	ImGui::EndDisabled();

	if (ImGui::BeginPopupModal("New Source", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::InputText("Title", newSourceTitleBuffer_, sizeof(newSourceTitleBuffer_));
		bool titleEmpty = newSourceTitleBuffer_[0] == '\0';
		ImGui::BeginDisabled(titleEmpty);
		if (ImGui::Button("Create Source")) {
			gedcomx::SourceDescription src;
			src.id = idgen::generateLocalId("S_");
			src.titles.push_back({ "", newSourceTitleBuffer_ });
			newSourceResult_ = src;
			pendingAutoSelectSourceId_ = src.id;
			newSourceTitleBuffer_[0] = '\0';
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if (!pendingAutoSelectSourceId_.empty()) {
		for (int i = 0; i < (int)knownSources.size(); ++i) {
			if (knownSources[i].id == pendingAutoSelectSourceId_) {
				selectedSourceIndex_ = 1;
				pendingAutoSelectSourceId_.clear();
				break;
			}
		}
	}

	if (editingExistingPerson_) {
		ImGui::Spacing();
		ImGui::TextDisabled("ID: %s", workingPerson_.id.c_str());
	}
}

void Window::renderPersonEditorNamesTab() {
	for (int i = 0; i < (int)editNames_.size(); ++i) {
		ImGui::PushID(i);
		ImGui::InputText("Given", editNames_[i].given, sizeof(editNames_[i].given));
		ImGui::InputText("Surname", editNames_[i].surname, sizeof(editNames_[i].surname));
		ImGui::SameLine();
		bool removed = false;
		if (ImGui::Button("Remove")) {
			editNames_.erase(editNames_.begin() + i);
			removed = true;
		}
		ImGui::PopID();
		if (removed) break;
	}

	if (ImGui::Button("+ Add Name")) {
		editNames_.push_back({});
	}
}

void Window::renderPersonEditorEventsTab() {
	for (int i = 0; i < (int)workingPerson_.facts.size(); ++i) {
		ImGui::PushID(i);
		const auto& fact = workingPerson_.facts[i];
		ImGui::BulletText("%s: %s %s", gedcomx::formatTypeUri(fact.type).c_str(),
			fact.date.original.c_str(), fact.place.original.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Edit")) {
			editingFactIndex_ = i;
			selectedFactTypeIndex_ = IM_ARRAYSIZE(kFactTypes) - 1;
			for (int t = 0; t < IM_ARRAYSIZE(kFactTypes) - 1; ++t) {
				if (fact.type == kFactTypes[t]) {
					selectedFactTypeIndex_ = t;
					break;
				}
			}
			strncpy_s(factTypeCustomBuffer_, fact.type.c_str(), sizeof(factTypeCustomBuffer_) - 1);
			strncpy_s(factDateBuffer_, fact.date.original.c_str(), sizeof(factDateBuffer_) - 1);
			strncpy_s(factPlaceBuffer_, fact.place.original.c_str(), sizeof(factPlaceBuffer_) - 1);
		}
		ImGui::SameLine();
		bool removed = false;
		if (ImGui::SmallButton("Remove")) {
			workingPerson_.facts.erase(workingPerson_.facts.begin() + i);
			if (editingFactIndex_ == i) editingFactIndex_ = -1;
			removed = true;
		}
		ImGui::PopID();
		if (removed) break;
	}

	ImGui::Separator();
	ImGui::Text(editingFactIndex_ >= 0 ? "Edit Event" : "Add Event");
	if (ImGui::BeginCombo("Type", selectedFactTypeIndex_ >= 0 &&  selectedFactTypeIndex_ < IM_ARRAYSIZE(kFactTypes)
		? gedcomx::formatTypeUri(kFactTypes[selectedFactTypeIndex_]).c_str() : "")) {
		for (int i = 0; i < IM_ARRAYSIZE(kFactTypes); ++i) {
			std::string label = gedcomx::formatTypeUri(kFactTypes[i]);
			if (ImGui::Selectable(label.c_str(), i == selectedFactTypeIndex_)) selectedFactTypeIndex_ = i;
		}
		ImGui::EndCombo();
	}
	bool isCustomType = selectedFactTypeIndex_ == IM_ARRAYSIZE(kFactTypes) - 1;
	if (isCustomType) ImGui::InputText("Custom Type URI", factTypeCustomBuffer_, sizeof(factTypeCustomBuffer_));
	ImGui::InputText("Date", factDateBuffer_, sizeof(factDateBuffer_));
	ImGui::InputText("Place", factPlaceBuffer_, sizeof(factPlaceBuffer_));

	bool noCustomType = isCustomType && factTypeCustomBuffer_[0] == '\0';
	ImGui::BeginDisabled(noCustomType);
	if (ImGui::Button(editingFactIndex_ >= 0 ? "Update Event" : "Add Event")) {
		gedcomx::Fact fact;
		fact.type = isCustomType ? factTypeCustomBuffer_ : kFactTypes[selectedFactTypeIndex_];
		fact.date.original = factDateBuffer_;
		fact.place.original = factPlaceBuffer_;

		if (editingFactIndex_ >= 0) {
			fact.id = workingPerson_.facts[editingFactIndex_].id;
			fact.conclusion = workingPerson_.facts[editingFactIndex_].conclusion;
			workingPerson_.facts[editingFactIndex_] = fact;
		}
		else {
			fact.id = idgen::generateLocalId("F_");
			workingPerson_.facts.push_back(fact);
		}

		editingFactIndex_ = -1;
		factDateBuffer_[0] = factPlaceBuffer_[0] = factTypeCustomBuffer_[0] = '\0';
		selectedFactTypeIndex_ = 0;
	}
	ImGui::EndDisabled();

	if (editingFactIndex_ >= 0) {
		ImGui::SameLine();
		if (ImGui::Button("Cancel Edit")) {
			editingFactIndex_ = -1;
			factDateBuffer_[0] = factPlaceBuffer_[0] = factTypeCustomBuffer_[0] = '\0';
			selectedFactTypeIndex_ = 0;
		}
	}
}

void Window::renderPersonEditorCitationsTab(const std::vector<gedcomx::SourceDescription>& knownSources) {
	for (int i = 0; i < (int)workingPerson_.conclusion.sources.size(); ++i) {
		ImGui::PushID(i);
		const auto& ref = workingPerson_.conclusion.sources[i];
		auto it = std::find_if(knownSources.begin(), knownSources.end(),
			[&](const gedcomx::SourceDescription& s) {
				return s.id == ref.sourceDescriptionId;
		});
		std::string label = it != knownSources.end() ? it->primaryTitle() : ref.sourceDescriptionId;
		ImGui::BulletText("%s", label.c_str());
		ImGui::SameLine();
		bool removed = false;
		if (ImGui::SmallButton("Remove")) {
			workingPerson_.conclusion.sources.erase(workingPerson_.conclusion.sources.begin() + i);
			removed = true;
		}
		ImGui::PopID();
		if (removed) break;
	}

	ImGui::Separator();
	if (ImGui::BeginCombo("##addSourceCombo",
		selectedSourceToAddIndex_ >= 0 && selectedSourceToAddIndex_ >= (int)knownSources.size()
		? knownSources[selectedSourceToAddIndex_].primaryTitle().c_str() : "Select a source to cite...")) {
		renderSelectableList<gedcomx::SourceDescription>(
			knownSources, &selectedSourceToAddIndex_,
			[](const gedcomx::SourceDescription& s) { return s.primaryTitle(); },
			[](const gedcomx::SourceDescription& s) { return s.id; }
		);
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	bool none = selectedSourceToAddIndex_ < 0 || selectedSourceToAddIndex_ >= (int)knownSources.size();
	ImGui::BeginDisabled(none);
	if (ImGui::Button("Add Citation")) {
		std::string id = knownSources[selectedSourceToAddIndex_].id;
		bool exists = std::any_of(workingPerson_.conclusion.sources.begin(), workingPerson_.conclusion.sources.end(),
			[&](const gedcomx::SourceReference& r) {
				return r.sourceDescriptionId == id;
			});
		if (!exists) workingPerson_.conclusion.sources.push_back({ id });
		selectedSourceToAddIndex_ = -1;
	}
	ImGui::EndDisabled();
}

void Window::renderPersonEditorAttributesTab() {
	for (int i = 0; i < (int)workingPerson_.attributes.size(); ++i) {
		ImGui::PushID(i);
		const auto& attr = workingPerson_.attributes[i];
		ImGui::BulletText("%s: %s", attr.type.c_str(), attr.value.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Edit")) {
			editingAttributeIndex_ = i;
			strncpy_s(attrTypeBuffer_, attr.type.c_str(), sizeof(attrTypeBuffer_) - 1);
			strncpy_s(attrValueBuffer_, attr.type.c_str(), sizeof(attrValueBuffer_) - 1);
		}
		ImGui::SameLine();
		bool removed = false;
		if (ImGui::SmallButton("Remove")) {
			workingPerson_.attributes.erase(workingPerson_.attributes.begin() + i);
			if (editingAttributeIndex_ == i) editingAttributeIndex_ = -1;
			removed = true;
		}
		ImGui::PopID();
		if (remove) true;
	}

	ImGui::Separator();
	ImGui::Text(editingAttributeIndex_ >= 0 ? "Edit Attribute" : "Add Attribute");
	ImGui::InputText("Type", attrTypeBuffer_, sizeof(attrTypeBuffer_));
	ImGui::InputText("Value", attrValueBuffer_, sizeof(attrValueBuffer_));

	bool empty = attrTypeBuffer_[0] == '\0' || attrValueBuffer_[0] == '\0';
	ImGui::BeginDisabled(empty);
	if (ImGui::Button(editingAttributeIndex_ >= 0 ? "Update Attribute" : "Add Attribute")) {
		gedcomx::Attribute attr;
		attr.type = attrTypeBuffer_;
		attr.value = attrValueBuffer_;

		if (editingAddressIndex_ >= 0) {
			attr.id = workingPerson_.attributes[editingAttributeIndex_].id;
			workingPerson_.attributes[editingAttributeIndex_] = attr;
		}
		else {
			attr.id = idgen::generateLocalId("AT_");
			workingPerson_.attributes.push_back(attr);
		}

		editingAttributeIndex_ = -1;
		attrTypeBuffer_[0] = attrValueBuffer_[0] = '\0';
	}													
	ImGui::EndDisabled();

	if (editingAttributeIndex_ >= 0) {
		ImGui::SameLine();
		if (ImGui::Button("Cancel Edit")) {
			editingAttributeIndex_ = -1;
			attrTypeBuffer_[0] = attrValueBuffer_[0] = '\0'; 
		}
	}
}

void Window::renderPersonEditorAddressessTab() {
	for (int i = 0; i < (int)workingPerson_.addresses.size(); ++i) {
		ImGui::PushID(i);
		const auto& addr = workingPerson_.addresses[i];
		ImGui::BulletText("%s, %s, %s %s", addr.city.c_str(), addr.stateOrProvince.c_str(), addr.country.c_str(), addr.postalCode.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Edit")) {
			editingAddressIndex_ = i;
			strncpy_s(addrStreetBuffer_, addr.street.c_str(), sizeof(addrStreetBuffer_) - 1);
			strncpy_s(addrCityBuffer_, addr.city.c_str(), sizeof(addrCityBuffer_) - 1);
			strncpy_s(addrStateBuffer_, addr.stateOrProvince.c_str(), sizeof(addrStateBuffer_) - 1);
			strncpy_s(addrPostalBuffer_, addr.postalCode.c_str(), sizeof(addrPostalBuffer_) - 1);
			strncpy_s(addrCountryBuffer_, addr.country.c_str(), sizeof(addrCountryBuffer_) - 1);
		}
		ImGui::SameLine();
		bool removed = false;
		if (ImGui::SmallButton("Remove")) {
			workingPerson_.addresses.erase(workingPerson_.addresses.begin() + i);
			if (editingAddressIndex_ == i) editingAddressIndex_ = -1;
			removed = true;
		}
		ImGui::PopID();
		if (removed) break;
	}

	ImGui::Separator();
	ImGui::Text(editingAddressIndex_ >= 0 ? "Edit Address" : "Add Address");
	ImGui::InputText("Street", addrStreetBuffer_, sizeof(addrStreetBuffer_));
	ImGui::InputText("City", addrCityBuffer_, sizeof(addrCityBuffer_));
	ImGui::InputText("State/Province", addrStateBuffer_, sizeof(addrStateBuffer_));
	ImGui::InputText("Postal Code", addrPostalBuffer_, sizeof(addrPostalBuffer_));
	ImGui::InputText("Country", addrCountryBuffer_, sizeof(addrCountryBuffer_));

	bool empty = addrCityBuffer_[0] == '\0' && addrCountryBuffer_[0] == '\0';
	ImGui::BeginDisabled(empty);
	if (ImGui::Button(editingAddressIndex_ >= 0 ? "Update Address" : "Add Address")) {
		gedcomx::Address addr;
		addr.street = addrStreetBuffer_;
		addr.city = addrCityBuffer_;
		addr.stateOrProvince = addrStateBuffer_;
		addr.postalCode = addrPostalBuffer_;
		addr.country = addrCountryBuffer_;

		if (editingAddressIndex_ >= 0) {
			addr.id = workingPerson_.addresses[editingAddressIndex_].id;
			workingPerson_.addresses[editingAddressIndex_] = addr;
		}
		else {
			addr.id = idgen::generateLocalId("AD_");
			workingPerson_.addresses.push_back(addr);
		}

		editingAddressIndex_ = -1;
		addrStreetBuffer_[0] = addrCityBuffer_[0] = addrStateBuffer_[0] = addrPostalBuffer_[0] = addrCountryBuffer_[0] = '\0';
	}
	ImGui::EndDisabled();

	if (editingAddressIndex_ >= 0) {
		ImGui::SameLine();
		if (ImGui::Button("Cancel Edit")) {
			editingAddressIndex_ = -1;
			addrStreetBuffer_[0] = addrCityBuffer_[0] = addrStateBuffer_[0] = addrPostalBuffer_[0] = addrCountryBuffer_[0] = '\0';
		}
	}
}

void Window::renderPersonEditor(const std::vector<gedcomx::SourceDescription>& knownSources) {
	if (showPersonEditor_ && !ImGui::IsPopupOpen("Person Editor")) {
		ImGui::OpenPopup("Person Editor");
	}

	ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
	if (ImGui::BeginPopupModal("Person Editor", &showPersonEditor_)) {
		renderPersonEditorTopStrip(knownSources);
		ImGui::Separator();

		if (ImGui::BeginTabBar("PersonEditorTabs")) {
			if (ImGui::BeginTabItem("Events")) {
				renderPersonEditorEventsTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Names")) {
				renderPersonEditorNamesTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Citations")) {
				renderPersonEditorCitationsTab(knownSources);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Attributes")) {
				renderPersonEditorAttributesTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Addresses")) {
				renderPersonEditorAddressessTab();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::Separator();
		bool noSource = selectedSourceIndex_ < 0 || selectedSourceIndex_ >= (int)knownSources.size();
		bool noName = primaryGivenBuffer_[0] == '\0' && primarySurnameBuffer_[0] == '\0';

		ImGui::BeginDisabled(noSource || noName);
		if (ImGui::Button("Save")) {
			gedcomx::Person result = workingPerson_;
			result.sourceId = knownSources[selectedSourceIndex_].id;
			result.id = editingExistingPerson_ ? workingPerson_.id : idgen::generateLocalId("P_");

			if (genderIndex_ >= 0 && genderIndex_ < IM_ARRAYSIZE(kGenderTypes))
				result.gender = gedcomx::Gender{ kGenderTypes[genderIndex_] };

			result.names.clear();

			gedcomx::Name primary;
			primary.title = primaryTitleBuffer_;
			primary.nickName = primaryNicknameBuffer_;
			primary.callName = primaryCallNameBuffer_;

			gedcomx::NameForm primaryForm;
			std::string given = primaryGivenBuffer_, surname = primarySurnameBuffer_;
			primaryForm.fullText = given + (given.empty() || surname.empty() ? "" : " ") + surname;
			if (!given.empty()) primaryForm.parts.push_back({ given, "http://gedcomx.org/Given" });
			if (!surname.empty()) primaryForm.parts.push_back({ surname, "http://gedcomx.org/Surname" });
			primary.nameForms.push_back(primaryForm);
			result.names.push_back(primary);

			for (auto& n : editNames_) {
				if (n.given[0] == '\0' && n.surname[0] == '\0') continue;
				gedcomx::NameForm form;
				std::string g = n.given, s = n.surname;
				form.fullText = g + (g.empty() || s.empty() ? "" : " ") + s;
				if (!g.empty()) form.parts.push_back({ g, "http://gedcomx.org/Given" });
				if (!s.empty()) form.parts.push_back({ s, "http://gedcomx.org/Surname" });
				gedcomx::Name name;
				name.nameForms.push_back(form);
				result.names.push_back(name);
			}

			personEditResult_ = std::make_pair(result, result.sourceId);
			showPersonEditor_ = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			showPersonEditor_ = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

std::optional<std::pair<gedcomx::Person, std::string>> Window::consumePersonEditResult() {
	auto result = personEditResult_;
	personEditResult_.reset();

	return result;
}
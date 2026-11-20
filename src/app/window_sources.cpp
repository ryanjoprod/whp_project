#include <cstring>

#include "window.h"
#include "id_generator.h"

void Window::openSourceEditor(SourceEditorOwner owner, const gedcomx::SourceDescription* existing) {
	sourceEditorOwner_ = owner;
	showSourceEditor_ = true;
	workingSource_ = existing ? *existing : gedcomx::SourceDescription{};

	strncpy_s(sourceTitleBuffer_, workingSource_.primaryTitle().c_str(), sizeof(sourceTitleBuffer_) - 1);
	strncpy_s(sourceAuthorBuffer_, workingSource_.author.c_str(), sizeof(sourceAuthorBuffer_) - 1);
	strncpy_s(sourcePubInfoBuffer_, workingSource_.publication.c_str(), sizeof(sourcePubInfoBuffer_) - 1);
	strncpy_s(sourceAbbrevBuffer_, workingSource_.abbreviation.c_str(), sizeof(sourceAbbrevBuffer_) - 1);
	fillEditableNotes(sourceNotes_, workingSource_.notes);
	fillEditableAttributes(sourceAttributes_, workingSource_.attributes);

	sourceReferences_.clear();
	sourceReferencesFor_.clear();
	if (!workingSource_.id.empty())
		pendingReferencesRequest_ = ReferencesRequest{ ReferencesKind::Source, workingSource_.id };
}

void Window::renderSourceEditor(SourceEditorOwner owner) {
	if (sourceEditorOwner_ != owner) return;  // only the place that asked for it renders it

	if (showSourceEditor_ && !ImGui::IsPopupOpen("Source Editor")) {
		ImGui::OpenPopup("Source Editor");
	}

	if (ImGui::BeginPopupModal("Source Editor", &showSourceEditor_, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::InputText("Title", sourceTitleBuffer_, sizeof(sourceTitleBuffer_));
		ImGui::InputText("Author", sourceAuthorBuffer_, sizeof(sourceAuthorBuffer_));
		ImGui::InputText("Pub. info.", sourcePubInfoBuffer_, sizeof(sourcePubInfoBuffer_));
		ImGui::InputText("Abbreviation", sourceAbbrevBuffer_, sizeof(sourceAbbrevBuffer_));
		ImGui::LabelText("ID", "%s", workingSource_.id.empty() ? "(assigned on OK)" : workingSource_.id.c_str());

		if (ImGui::BeginTabBar("SourceEditorTabs")) {
			if (ImGui::BeginTabItem("Notes")) {
				renderEditableNotes(sourceNotes_);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Attributes")) {
				renderEditableAttributes(sourceAttributes_);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("References")) {
				if (workingSource_.id.empty() || sourceReferencesFor_ != workingSource_.id)
					ImGui::TextDisabled("Nothing cites this yet.");
				else
					renderReferencesTab(sourceReferences_, true);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::Separator();
		ImGui::BeginDisabled(sourceTitleBuffer_[0] == '\0');
		if (ImGui::Button("OK")) {
			gedcomx::SourceDescription src = workingSource_;  // keeps the id and the citation text we don't edit yet
			const bool isNew = src.id.empty();
			if (isNew) src.id = idgen::generateLocalId("S_");

			if (src.titles.empty()) src.titles.push_back({ "", sourceTitleBuffer_ });
			else src.titles[0].value = sourceTitleBuffer_;

			src.author = sourceAuthorBuffer_;
			src.publication = sourcePubInfoBuffer_;
			src.abbreviation = sourceAbbrevBuffer_;
			src.notes = collectNotes(sourceNotes_);
			src.attributes = collectAttributes(sourceAttributes_);

			sourceSaveResult_ = src;
			// Only the Person Editor wants the new source picked for it
			if (isNew && owner == SourceEditorOwner::PersonEditor)
				pendingAutoSelectSourceId_ = src.id;

			showSourceEditor_ = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			showSourceEditor_ = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (!showSourceEditor_) sourceEditorOwner_ = SourceEditorOwner::None;
}

std::optional<gedcomx::SourceDescription> Window::consumeSourceSave() {
	auto result = sourceSaveResult_;
	sourceSaveResult_.reset();
	return result;
}
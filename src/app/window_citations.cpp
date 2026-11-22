#include <algorithm>
#include <cfloat>
#include <cstring>

#include "window.h"
#include "id_generator.h"
#include "gedcomx/citation.h"

namespace {
	// GEDCOM X confidence levels. Index 0 means "not set".
	constexpr const char* kCitationConfidence[] = {
		"", "http://gedcomx.org/Low", "http://gedcomx.org/Medium", "http://gedcomx.org/High"
	};

	const gedcomx::Citation* findCitation(const std::vector<gedcomx::Citation>& citations, const std::string& id) {
		auto it = std::find_if(citations.begin(), citations.end(),
			[&](const gedcomx::Citation& c) {
				return c.id == id;
		});
		return it != citations.end() ? &*it : nullptr;
	}

	std::string sourceTitle(const std::vector<gedcomx::SourceDescription>& sources, const std::string& sourceId) {
		auto it = std::find_if(sources.begin(), sources.end(),
			[&](const gedcomx::SourceDescription& s) {
				return s.id == sourceId;
		});
		return it != sources.end() ? it->primaryTitle() : sourceId;
	}

	// "Source title - p. 12" or "Source title - whole source"
	std::string citationLabel(const gedcomx::SourceReference& ref,
		const std::vector<gedcomx::Citation>& citations,
		const std::vector<gedcomx::SourceDescription>& sources) {
		if (const auto* c = findCitation(citations, ref.citationId))
			return sourceTitle(sources, c->sourceId) + " - " + gedcomx::citationSummary(*c);
		// Not in the list (a bare citation of a breand-new source, say): the id still says enough
		if (auto sourceId = gedcomx::bareCitationSourceId(ref.citationId))
			return sourceTitle(sources, *sourceId) + " - whole source";
		return ref.citationId;
	}
}

std::optional<gedcomx::Citation> Window::consumeCitationSave() {
	auto result = citationSaveResults_;
	citationSaveResults_.reset();
	return result;
}

std::vector<gedcomx::SourceReference>& Window::citationTargetRefs() {
	return citationEditorOwner_ == CitationEditorOwner::Relationship
		? workingRelationship_.conclusion.sources
		: workingPerson_.conclusion.sources;
}

// --- The list on the Citations tabs ---

void Window::renderCitationRefs(CitationEditorOwner owner, std::vector<gedcomx::SourceReference>& refs, const std::vector<gedcomx::SourceDescription>& knownSources) {
	for (int i = 0; i < (int)refs.size(); ++i) {
		ImGui::PushID(i);
		ImGui::BulletText("%s", citationLabel(refs[i], knownCitations_, knownSources).c_str());
		ImGui::SameLine();

		const gedcomx::Citation* citation = findCitation(knownCitations_, refs[i].citationId);
		if (citation && !gedcomx::isBare(*citation)) {  // a bare citation has nothing to edit
			if (ImGui::SmallButton("Edit")) openCitationEditor(owner, citation);
			ImGui::SameLine();
		}

		bool removed = ImGui::SmallButton("Remove");
		ImGui::PopID();
		if (removed) { 
			refs.erase(refs.begin() + i);
			break;
		}
	}

	ImGui::Separator();
	if (ImGui::Button("Add Citation...")) openCitationEditor(owner, nullptr);
}

// --- The editor ---

void Window::startNewCitationForm() {
	workingCitation_ = gedcomx::Citation{};
	citationDate_ = gedcomx::Date{};
	citationPageBuffer_[0] = citationDateBuffer_[0] = '\0';
	citationConfidenceIndex_ = 0;
}

void Window::fillCitationForm(const gedcomx::Citation& c) {
	workingCitation_ = c;
	strncpy_s(citationPageBuffer_, c.page.c_str(), sizeof(citationPageBuffer_) - 1);
	strncpy_s(citationDateBuffer_, c.date.original.c_str(), sizeof(citationDateBuffer_) - 1);
	citationDate_ = c.date;  // the whole date, with its formal form, calendar and sort value

	citationConfidenceIndex_ = 0;
	for (int i = 0; i < IM_ARRAYSIZE(kCitationConfidence); ++i)
		if (c.confidence == kCitationConfidence[i]) citationConfidenceIndex_ = i;

	citationNotes_.clear();
	fillEditableNotes(citationNotes_, c.notes);
	fillEditableAttributes(citationAttributes_, c.attributes);

	citationReferences_.clear();
	citationReferencesFor_.clear();
	if (!workingCitation_.id.empty())
		pendingReferencesRequest_ = ReferencesRequest{ ReferencesKind::Citation, c.id };
}

void Window::openCitationEditor(CitationEditorOwner owner, const gedcomx::Citation* existing) {
	citationEditorOwner_ = owner;
	showCitationEditor_ = true;
	citationSourceLocked_ = existing != nullptr;

	if (existing) fillCitationForm(*existing);
	else startNewCitationForm();

	pendingCitationSourceId_ = existing ? existing->sourceId : lastCitedSourceId_;
	citationSourceIndex_ = -1;
}

void Window::renderEditableNotes(std::vector<EditableNote>& notes) {
	for (int i = 0; i < (int)notes.size(); ++i) {
		ImGui::PushID(i);
		ImGui::InputTextMultiline("##note", notes[i].text, sizeof(notes[i].text), ImVec2(-FLT_MIN, 70.0f));
		bool removed = ImGui::SmallButton("Remove note");
		ImGui::PopID();
		if (removed) {
			notes.erase(notes.begin() + i);
			break;
		}
	}
	if (ImGui::Button("Add note")) notes.emplace_back();
}

void Window::fillEditableNotes(std::vector<EditableNote>& out, const std::vector<gedcomx::Note>& notes) {
	out.clear();
	for (const auto& n : notes) {
		EditableNote note;
		note.lang = n.lang;
		note.subject = n.subject;
		strncpy_s(note.text, n.text.c_str(), sizeof(note.text) - 1);
		out.push_back(std::move(note));
	}
}

std::vector<gedcomx::Note> Window::collectNotes(const std::vector<EditableNote>& notes) {
	std::vector<gedcomx::Note> out;
	for (const auto& n : notes) {
		if (n.text[0] == '\0') continue;  // an empty note is note a note
		out.push_back({ n.lang, n.subject, n.text });
	}

	return out;
}

void Window::renderEditableAttributes(std::vector<EditableAttribute>& attributes) {
	for (int i = 0; i < (int)attributes.size(); ++i) {
		ImGui::PushID(i);
		ImGui::SetNextItemWidth(140.0f);
		ImGui::InputTextWithHint("##type", "Type", attributes[i].type, sizeof(attributes[i].type));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(260.0f);
		ImGui::InputTextWithHint("##value", "Value", attributes[i].value, sizeof(attributes[i].value));
		ImGui::SameLine();
		bool removed = ImGui::SmallButton("Remove");
		ImGui::PopID();
		if (removed) {
			attributes.erase(attributes.begin() + i); 
			break;
		}
	}
	if (ImGui::Button("Add attribute"))
		attributes.emplace_back();
}

void Window::fillEditableAttributes(std::vector<EditableAttribute>& out, const std::vector<gedcomx::Attribute>& attributes) {
	out.clear();
	for (const auto& a : attributes) {
		EditableAttribute row;
		row.id = a.id;
		strncpy_s(row.type, a.type.c_str(), sizeof(row.type) - 1);
		strncpy_s(row.value, a.value.c_str(), sizeof(row.value) - 1);
		out.push_back(std::move(row));
	}
}

std::vector<gedcomx::Attribute> Window::collectAttributes(const std::vector<EditableAttribute>& attributes) {
	std::vector<gedcomx::Attribute> out;
	for (const auto& row : attributes) {
		if (row.type[0] == '\0' && row.value[0] == '\0') continue;  // an empty row is not an attribute
		out.push_back({ row.id.empty() ? idgen::generateLocalId("AT_") : row.id, row.type, row.value });
	}

	return out;
}

void Window::setReferences(ReferencesKind kind, const std::string& id, std::vector<storage::Reference> refs) {
	if (kind == ReferencesKind::Source) {
		sourceReferencesFor_ = id;
		sourceReferences_ = std::move(refs);
	}
	else {
		citationReferencesFor_ = id;
		citationReferences_ = std::move(refs);
	}
}

void Window::renderReferencesTab(const std::vector<storage::Reference>& refs, bool showCitationColumn) {
	if (refs.empty()) {
		ImGui::TextDisabled("Nothing cites this yet.");
		return;
	}

	auto citationText = [&](const std::string& id) {
		for (const auto& c : knownCitations_)
			if (c.id == id) return gedcomx::citationSummary(c);  // a bare citation	reads "whole source"
		return id;
	};

	const int columns = showCitationColumn ? 3 : 2;
	if (ImGui::BeginTable("##references", columns, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(520.0f, 180.0f))) {
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.f);
		ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
		if (showCitationColumn) ImGui::TableSetupColumn("Citation", ImGuiTableColumnFlags_WidthFixed, 140.f);
		ImGui::TableHeadersRow();
		for (const auto& r : refs) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(r.kind.c_str());
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(r.label.c_str());
			if (showCitationColumn) {
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(citationText(r.citationId).c_str());
			}
		}
		ImGui::EndTable();
	}
}

void Window::renderCitationEditor(CitationEditorOwner owner, const std::vector<gedcomx::SourceDescription>& knownSources) {
	if (citationEditorOwner_ != owner) return;  // only the editor that asked for it renders it

	if (showCitationEditor_ && !ImGui::IsPopupOpen("Citation Editor")) {
		ImGui::OpenPopup("Citation Editor");
	}
	
	if (ImGui::BeginPopupModal("Citation Editor", &showCitationEditor_, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (!pendingCitationSourceId_.empty()) {
			for (int i = 0; i < (int)knownSources.size(); ++i) {
				if (knownSources[i].id == pendingCitationSourceId_) {
					citationSourceIndex_ = i;
					break;
				}
			}
			pendingCitationSourceId_.clear();
		}

		// Source
		const int sourceBefore = citationSourceIndex_;
		bool validSource = citationSourceIndex_ >= 0 && citationSourceIndex_ < (int)knownSources.size();
		ImGui::BeginDisabled(citationSourceLocked_);
		if (ImGui::BeginCombo("Source", validSource ? knownSources[citationSourceIndex_].primaryTitle().c_str() : "Select a source...")) {
			renderSelectableList<gedcomx::SourceDescription>(
				knownSources, &citationSourceIndex_,
				[](const gedcomx::SourceDescription& s) { return s.primaryTitle(); },
				[](const gedcomx::SourceDescription& s) { return s.id; }
			);
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();

		// A loaded ciation belongs to its own source; picking another source starts a new one.
		if (citationSourceIndex_ != sourceBefore && !workingCitation_.id.empty()) startNewCitationForm();
		validSource = citationSourceIndex_ >= 0 && citationSourceIndex_ < (int)knownSources.size();

		// Reuse a citation this source already has
		if (!citationSourceLocked_ && validSource) {
			const std::string& sourceId = knownSources[citationSourceIndex_].id;
			std::vector<const gedcomx::Citation*> existing;
			for (const auto& c : knownCitations_)
				if (c.sourceId == sourceId && !gedcomx::isBare(c))
					existing.push_back(&c);

			if (!existing.empty()) {
				std::string preview = workingCitation_.id.empty() ? "New citation" : gedcomx::citationSummary(workingCitation_);
				if (ImGui::BeginCombo("Citation", preview.c_str())) {
					if (ImGui::Selectable("New citation", workingCitation_.id.empty()))	startNewCitationForm();
					for (const auto* c : existing) {
						std::string label = gedcomx::citationSummary(*c) + "##" + c->id;
						if (ImGui::Selectable(label.c_str(), workingCitation_.id == c->id)) fillCitationForm(*c);
					}
					ImGui::EndCombo();
				}
			}
		}
		if (!workingCitation_.id.empty())
			ImGui::TextDisabled("Editing a shared citation changes it everywhere it is used.");
		else
			ImGui::TextDisabled("Leave the details blank to cite the whole source.");

		ImGui::InputText("Volume/Page", citationPageBuffer_, sizeof(citationPageBuffer_));
		
		// the words can be typed (a text-only date); "..." opens the Date editor for the structured form
		ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - 36.0f);
		if (ImGui::InputText("##citationDate", citationDateBuffer_, sizeof(citationDateBuffer_))) {
			gedcomx::DateParts typed;
			typed.kind = gedcomx::DateKind::TextOnly;
			typed.text = citationDateBuffer_;
			citationDate_ = gedcomx::toDate(typed);
		}
		ImGui::SameLine(0.0f, 4.0f);
		if (ImGui::Button("...##citationDate")) openDateEditor(DateEditorTarget::Citation, citationDate_);
		ImGui::SameLine();
		ImGui::TextUnformatted("Date");

		std::string confPreview = citationConfidenceIndex_ == 0 ? "(none)" : gedcomx::formatTypeUri(kCitationConfidence[citationConfidenceIndex_]);
		if (ImGui::BeginCombo("Confidence", confPreview.c_str())) {
			for (int i = 0; i < IM_ARRAYSIZE(kCitationConfidence); ++i) {
				std::string label = i == 0 ? "(none)" : gedcomx::formatTypeUri(kCitationConfidence[i]);
				if (ImGui::Selectable(label.c_str(), citationConfidenceIndex_ == i)) citationConfidenceIndex_ = i;
			}
			ImGui::EndCombo();
		}
		ImGui::LabelText("ID", "%s", workingCitation_.id.empty() ? "(assigned on OK)" : workingCitation_.id.c_str());

		if (ImGui::BeginTabBar("CitationEditorTabs")) {
			if (ImGui::BeginTabItem("Notes")) {
				renderEditableNotes(citationNotes_);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Attributes")) {
				renderEditableAttributes(citationAttributes_);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("References")) {
				if (workingCitation_.id.empty() || citationReferencesFor_ != workingCitation_.id)
					ImGui::TextDisabled("Nothing cites this yet.");
				else
					renderReferencesTab(citationReferences_, false);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::Separator();
		ImGui::BeginDisabled(!validSource);
		if (ImGui::Button("OK")) {
			gedcomx::Citation c = workingCitation_;  // keeps the id when editing, and the formal date
			c.sourceId = knownSources[citationSourceIndex_].id;
			c.page = citationPageBuffer_;
			c.date = citationDate_;
			c.confidence = kCitationConfidence[citationConfidenceIndex_];
			c.notes = collectNotes(citationNotes_);
			c.attributes = collectAttributes(citationAttributes_);

			std::string refId;
			if (c.id.empty() && gedcomx::isBlankCitation(c)) {
				refId = gedcomx::bareCitationId(c.sourceId);  // whole source; its bare citation already exists
			}
			else {
				if (c.id.empty()) c.id = idgen::generateLocalId("C_");
				refId = c.id;
				citationSaveResults_ = c;  // App saves it right away, before the person or relationship is saved
			}

			auto& refs = citationTargetRefs();
			bool already = std::any_of(refs.begin(), refs.end(),
				[&](const gedcomx::SourceReference& r) {
					return r.citationId == refId;
				});
			if (!already) refs.push_back({ refId });

			lastCitedSourceId_ = c.sourceId;
			showCitationEditor_ = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			showCitationEditor_ = false;
			ImGui::CloseCurrentPopup();
		}

		if (auto picked = renderDateEditor(DateEditorTarget::Citation)) {
			citationDate_ = *picked;
			citationDateBuffer_[0] = '\0';
			strncpy_s(citationDateBuffer_, picked->original.c_str(), sizeof(citationDateBuffer_) - 1);
		}

		ImGui::EndPopup();
	}

	if (!showCitationEditor_) citationEditorOwner_ = CitationEditorOwner::None;
}
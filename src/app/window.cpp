#include <stdexcept>
#include <random>
#include <algorithm>
#include <cfloat>
#include <unordered_set>
// #include <iostream>  // used for debug

#include <SDL3/SDL_dialog.h>

#include "window.h"

namespace {
	constexpr float kTitleBarHeight = 32.0f;
}

Window::Window() {
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
	}

	window_ = SDL_CreateWindow("WHP - Genealogy Visualizer", 1280, 720, SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE);
	if (!window_) {
		SDL_Quit();
		throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
	}

	renderer_ = SDL_CreateRenderer(window_, nullptr);
	if (!renderer_) {
		SDL_DestroyWindow(window_);
		SDL_Quit();
		throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
	ImGui_ImplSDLRenderer3_Init(renderer_);
}

Window::~Window() {
	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	SDL_DestroyRenderer(renderer_);
	SDL_DestroyWindow(window_);
	SDL_Quit();
}

bool Window::isRunning() const {
	return running_;
}

void Window::processEvents() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		ImGui_ImplSDL3_ProcessEvent(&event);
		if (event.type == SDL_EVENT_QUIT) {
			running_ = false;
		}
	}
}

void Window::beginFrame() {
	ImGui_ImplSDLRenderer3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
}

void Window::renderTitleBarAndMenu() {
	ImGuiViewport* viewport = ImGui::GetMainViewport();

	// --- Custom Title bar ---
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, kTitleBarHeight));

	ImGuiWindowFlags titleBarFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar;

	ImGui::Begin("TitleBar", nullptr, titleBarFlags);

	// --- Menu bar ---
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Open Source File...")) {
				requestOpenFileDialog();
			}
			ImGui::MenuItem("Open Project...");
			ImGui::Separator();
			if (ImGui::MenuItem("Exit")) {
				running_ = false;
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View")) {
			ImGui::MenuItem("Layers");
			ImGui::EndMenu();
		}

		ImGui::Text("  WHP - Geneaology Visualizer");

		// Right-aligned close button
		ImGui::SameLine(ImGui::GetWindowWidth() - 40);
		if (ImGui::Button("X", ImVec2(30, 0))) {
			running_ = false;
		}

		ImGui::EndMenuBar();
	}

	// Drag-to-move: uses absolute screen coordinates so movement doesn't
	// lag behind or feed back on itself as the window's own position changes
	bool titleBarHovered = ImGui::IsWindowHovered();
	if (titleBarHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
		draggingWindow_ = true;
		SDL_GetGlobalMouseState(&dragStartMouseX_, &dragStartMouseY_);
		SDL_GetWindowPosition(window_, &dragStartWindowX_, &dragStartWindowY_);
	}
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		draggingWindow_ = false;
	}

	if (draggingWindow_) {
		float currentMouseX, currentMouseY;
		SDL_GetGlobalMouseState(&currentMouseX, &currentMouseY);
		int dx = (int)(currentMouseX - dragStartMouseX_);
		int dy = (int)(currentMouseY - dragStartMouseY_);
		SDL_SetWindowPosition(window_, dragStartWindowX_ + dx, dragStartWindowY_ + dy);
	}

	ImGui::End();
}

void Window::updateLayout(const std::vector<gedcomx::Person>& people, const std::vector<gedcomx::Relationship>& relationships) {
	static std::mt19937 rng(12345);  // fixed seed: reproducible layout between runs
	std::uniform_real_distribution<float> jitter(-50.0f, 50.0f);

	// Physics operates in a fixed world-space frame, completely independent
	// of screen position/size or the camera. World (0,0) is just "wherever
	// the graph settles" - the camera decides how that maps to the screen.
	ImVec2 worldCenter(0.0f, 0.0f);
	for (const auto& person : people) {
		if (nodePositions_.find(person.id) == nodePositions_.end()) {
			nodePositions_[person.id] = ImVec2(worldCenter.x + jitter(rng), worldCenter.y + jitter(rng));
			nodeVelocities_[person.id] = ImVec2(0, 0);
		}
	}

	// Forces
	const float repulsionStrength = 8000.0f;
	const float springStrength = 0.02f;
	const float restLength = 150.0f;
	const float damping = 0.85f;
	const float centeringStrength = 0.01f;

	std::unordered_map<std::string, ImVec2> forces;
	for (const auto& person : people) forces[person.id] = ImVec2(0, 0);

	// Repulsion: every pair of nodes pushes apart, weaker at greater distance
	for (size_t i = 0; i < people.size(); ++i) {
		for (size_t j = i + 1; j < people.size(); ++j) {
			ImVec2& posA = nodePositions_[people[i].id];
			ImVec2& posB = nodePositions_[people[j].id];

			float dx = posA.x - posB.x;
			float dy = posA.y - posB.y;
			float distSq = std::max(dx * dx + dy * dy, 1.0f);
			float dist = std::sqrt(distSq);

			float force = repulsionStrength / distSq;
			float fx = (dx / dist) * force;
			float fy = (dy / dist) * force;

			forces[people[i].id].x += fx;
			forces[people[i].id].y += fy;
			forces[people[j].id].x -= fx;
			forces[people[j].id].y -= fy;
		}
	}

	// Attraction: relationships pull their two people toward restLength apart.
	for (const auto& rel : relationships) {
		if (!nodePositions_.count(rel.person1Id) || !nodePositions_.count(rel.person2Id)) continue;

		ImVec2& posA = nodePositions_[rel.person1Id];
		ImVec2& posB = nodePositions_[rel.person2Id];

		float dx = posB.x - posA.x;
		float dy = posB.y - posA.y;
		float dist = std::max(std::sqrt(dx * dx + dy * dy), 1.0f);

		float force = springStrength * (dist - restLength);
		float fx = (dx / dist) * force;
		float fy = (dy / dist) * force;

		forces[rel.person1Id].x += fx;
		forces[rel.person1Id].y += fy;
		forces[rel.person2Id].x -= fx;
		forces[rel.person2Id].y -= fy;
	}

	// Centering: a weak pull toward worldCenter keeps disconnected modes
	// (and the graph as a whole) from drifting outward indefinitely.
	for (const auto& person : people) {
		ImVec2& pos = nodePositions_[person.id];
		forces[person.id].x += (worldCenter.x - pos.x) * centeringStrength;
		forces[person.id].y += (worldCenter.y - pos.y) * centeringStrength;
	}

	// Inegrate: apply forces to velocity, velocity to position, then damp.
	for (const auto& person : people) {
		if (person.id == draggedNodeId_) continue;  // user has direct control right	now

		ImVec2& vel = nodeVelocities_[person.id];
		ImVec2& pos = nodePositions_[person.id];
		ImVec2& f = forces[person.id];

		vel.x = (vel.x + f.x) * damping;
		vel.y = (vel.y + f.y) * damping;

		const float maxSpeed = 500.0f;
		float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
		if (speed > maxSpeed) {
			vel.x = (vel.x / speed) * maxSpeed;
			vel.y = (vel.y / speed) * maxSpeed;
		}

		pos.x += vel.x * 0.016f;  // roughly one frame's worth of time at 60fps
		pos.y += vel.y * 0.016f;
	}
}

void Window::renderCanvas(const std::vector<gedcomx::Person>& people, const std::vector<gedcomx::Relationship>& relationships) {
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	// --- Canvas placeholder: fills the rest of the window ---
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + kTitleBarHeight));
	ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - kTitleBarHeight));

	ImGuiWindowFlags canvasFlags =
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::Begin("Canvas", nullptr, canvasFlags);

	// --- Node dragging / click selection ---
	ImVec2 mousePos = ImGui::GetIO().MousePos;
	const float clickMoveTolerance = 4.0f;

	if (ImGui::IsWindowHovered()) {
		// Right-drdag to pan (left-drag is already claimed by node dragging).
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
			ImVec2 delta = ImGui::GetIO().MouseDelta;
			camera_.pan(delta);
		}

		float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0.0f) {
			float zoomFactor = (wheel > 0) ? 1.1f : (1.0f / 1.1f);
			camera_.zoomAt(mousePos, zoomFactor);
		}
	}
	
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 canvasOrigin = ImGui::GetWindowPos();  // top-left of this window, in screen coordinates
	ImVec2 canvasSize = ImGui::GetWindowSize();
	camera_.setViewport(canvasOrigin, canvasSize);

	updateLayout(people, relationships);

	// Drop any cached position/velocity for people no longer in the visible set
	// (evicted by App's cap, or simply no longer reachable after a re-root).
	std::unordered_set<std::string> currentIds;
	for (const auto& person : people) currentIds.insert(person.id);

	for (auto it = nodePositions_.begin(); it != nodePositions_.end(); ) {
		if (!currentIds.count(it->first)) {
			nodeVelocities_.erase(it->first);
			it = nodePositions_.erase(it);
		}
		else {
			++it;
		}
	}

	if (needsRefit_ && !people.empty()) {
		ImVec2 minB(FLT_MAX, FLT_MAX), maxB(-FLT_MAX, -FLT_MAX);
		for (const auto& person : people) {
			ImVec2 pos = nodePositions_.count(person.id) ? nodePositions_[person.id] : ImVec2(0, 0);
			// std::cout << "  node " << person.id << " at (" << pos.x << "," << pos.y << ")\n";  // debug code
			minB.x = std::min(minB.x, pos.x);
			minB.y = std::min(minB.y, pos.y);
			maxB.x = std::max(maxB.x, pos.x);
			maxB.y = std::max(maxB.y, pos.y);
			// std::cout << "Refit bounds: min(" << minB.x << "," << minB.y << ") max(" << maxB.x << "," << maxB.y << ")\n";  // debug code
		}

		camera_.refitToBounds(minB, maxB);
		needsRefit_ = false;
		// std::cout << "After refit: zoom = " << camera_.zoom() << ", viewportSize = (" << canvasSize.x << "," << canvasSize.y << ")\n";  // debug code
	}

	const float nodeRadius = 20.0f;

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && draggedNodeId_.empty() && ImGui::IsWindowHovered()) {
		// Hit-test: is the mouse over any node? Check topmost-drawn (last in
		// the list) first, since that's what the user visually sees on top.
		for (auto it = people.rbegin(); it != people.rend(); ++it) {
			ImVec2 screenPos = camera_.worldToScreen(nodePositions_[it->id]);
			float dx = mousePos.x - screenPos.x;
			float dy = mousePos.y - screenPos.y;
			float hitRadius = nodeRadius * camera_.zoom();
			if (dx * dx + dy * dy <= hitRadius * hitRadius) {
				draggedNodeId_ = it->id;
				dragStartMousePos_ = mousePos;
				break;
			}
		}
	}

	if (!draggedNodeId_.empty()) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			// Pin the dragged node directly to the cursor; zero its velocity
			// so physics doesn't fling it once released.
			nodePositions_[draggedNodeId_] = camera_.screenToWorld(mousePos);
			nodeVelocities_[draggedNodeId_] = ImVec2(0, 0);
		}
		else {
			// Mouse released: decide whether this was a click or a drag based
			// on how far the mouse moved since the press started.
			float dx = mousePos.x - dragStartMousePos_.x;
			float dy = mousePos.y - dragStartMousePos_.y;
			if (dx * dx + dy * dy <= clickMoveTolerance * clickMoveTolerance) {
				selectedPersonId_ = draggedNodeId_;
			}
			draggedNodeId_.clear();  // mouse released - hand back to physics
		}
	}

	// Edges first, so nodes render on top of the lines
	// rather than lines being drawn over the nodes.
	for (const auto& rel : relationships) {
		auto it1 = nodePositions_.find(rel.person1Id);
		auto it2 = nodePositions_.find(rel.person2Id);
		if (it1 != nodePositions_.end() && it2 != nodePositions_.end()) {
			drawList->AddLine(camera_.worldToScreen(it1->second), camera_.worldToScreen(it2->second), IM_COL32(150, 150, 150, 255), 2.0f);
		}
	}

	// Third pass: draw nodes + labels on top.
	for (const auto& person : people) {
		ImVec2 worldPos = nodePositions_[person.id];
		ImVec2 screenPos = camera_.worldToScreen(worldPos);
		float screenRadius = nodeRadius * camera_.zoom();

		drawList->AddCircleFilled(screenPos, screenRadius, IM_COL32(70, 130, 200, 255));
		drawList->AddCircle(screenPos, screenRadius, IM_COL32(255, 255, 255, 255), 0, 2.0f);

		// LOD: skip labels once zoomed out far enough that text won't be legible anyway.
		if (camera_.zoom() > 0.4f) {
			std::string label = person.primaryFullText();
			ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
			ImVec2 textPos(screenPos.x - textSize.x * 0.5f, screenPos.y + nodeRadius + 4.0f);
			drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), label.c_str());
		}
	}

	ImGui::End();  // Close the "Canvas" window

	renderDetailsPanel(people);
}

void Window::renderDetailsPanel(const std::vector<gedcomx::Person>& people) {
	if (selectedPersonId_.empty()) return;

	auto it = std::find_if(people.begin(), people.end(),
		[&](const gedcomx::Person& p) { return p.id == selectedPersonId_; });
	if (it == people.end()) return;  // selected person isn't currently visible; nothing to show

	const gedcomx::Person& person = *it;

	ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
	bool open = true;
	if (ImGui::Begin("Person Details", &open)) {
		ImGui::Text("%s", person.primaryFullText().c_str());
		ImGui::Separator();

		if (!person.sourceId.empty()) {
			ImGui::TextDisabled("Source: %s", person.sourceId.c_str());
		}

		if (!person.facts.empty()) {
			ImGui::Spacing();
			ImGui::Text("Facts:");
			for (const auto& fact : person.facts) {
				ImGui::BulletText("%s: %s %s",
					gedcomx::formatFactType(fact.type).c_str(),
					fact.date.original.c_str(),
					fact.place.original.c_str());
			}
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	if (ImGui::Button("Explore from here")) {
		reRootRequestId_ = person.id;
	}

	ImGui::End();

	if (!open) selectedPersonId_.clear();  // user clicked the panel's own close button
}

std::string Window::consumeReRootRequest() {
	std::string result = reRootRequestId_;
	reRootRequestId_.clear();
	return result;
}

void Window::requestOpenFileDialog() {
	SDL_DialogFileFilter filters[] = {
		{ "GEDCOM X (JSON)", "json" },
		{ "All files", "*" }
	};

	SDL_ShowOpenFileDialog(
		&Window::handleFileDialogResult,
		this,  // userdata: how the static callback gets back to this instance 
		window_,
		filters,
		2,  // nFilters
		nullptr,  // default_location: let the OS pick
		true  // allow_many: let the user select multiple files at once
	);
}

void Window::handleFileDialogResult(void* userdata, const char* const* filelist, int filter) {
	Window* self = static_cast<Window*>(userdata);

	if (!filelist) {
		// SDL signals an error this way - filelist is null, not empty
		return;
	}

	std::lock_guard<std::mutex> lock(self->openedFilesMutex_);
	for (int i = 0; filelist[i] != nullptr; ++i) {
		self->openedFiles_.push_back(filelist[i]);
	}
}

std::vector<std::string> Window::consumeOpenedFiles() {
	std::lock_guard<std::mutex> lock(openedFilesMutex_);
	std::vector<std::string> result = std::move(openedFiles_);
	openedFiles_.clear();

	return result;
}

void Window::endFrame() {
	ImGui::Render();
	SDL_SetRenderDrawColor(renderer_, 30, 30, 30, 255);
	SDL_RenderClear(renderer_);
	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
	SDL_RenderPresent(renderer_);
}
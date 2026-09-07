#include <stdexcept>
#include <random>

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
			ImGui::MenuItem("Open Source File...");
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

void Window::updateLayout(const std::vector<gedcomx::Person>& people, const std::vector<gedcomx::Relationship>& relationships, ImVec2 canvasOrigin, ImVec2 canvasSize) {
	static std::mt19937 rng(12345);  // fixed seed: reproducible layout between runs
	std::uniform_real_distribution<float> jitter(-50.0f, 50.0f);

	// New nodes start near the canvas center with a little randn jitter,
	// so two brand-new nodes don't spawn exactly on top of each other
	// (which would make the repulsion force divide by zero).
	ImVec2 center(canvasOrigin.x + canvasSize.x * 0.5f, canvasOrigin.y + canvasSize.y * 0.5f);
	for (const auto& person : people) {
		if (nodePositions_.find(person.id) == nodePositions_.end()) {
			nodePositions_[person.id] = ImVec2(center.x + jitter(rng), center.y + jitter(rng));
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

	// Centering: a weak pull toward canvas center keeps disconnected modes
	// (and the graph as a whole) from drifting outward indefinitely.
	for (const auto& person : people) {
		ImVec2& pos = nodePositions_[person.id];
		forces[person.id].x += (center.x - pos.x) * centeringStrength;
		forces[person.id].y += (center.y - pos.y) * centeringStrength;
	}

	// Inegrate: apply forces to velocity, velocity to position, then damp.
	for (const auto& person : people) {
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
	
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 canvasOrigin = ImGui::GetWindowPos();  // top-left of this window, in screen coordinates

	const float nodeRadius = 20.0f;

	updateLayout(people, relationships, canvasOrigin, ImGui::GetWindowSize());

	// Edges first, so nodes render on top of the lines
	// rather than lines being drawn over the nodes.
	for (const auto& rel : relationships) {
		auto it1 = nodePositions_.find(rel.person1Id);
		auto it2 = nodePositions_.find(rel.person2Id);
		if (it1 != nodePositions_.end() && it2 != nodePositions_.end()) {
			drawList->AddLine(it1->second, it2->second, IM_COL32(150, 150, 150, 255), 2.0f);
		}
	}

	// Third pass: draw nodes + labels on top.
	for (const auto& person : people) {
		ImVec2 nodeCenter = nodePositions_[person.id];

		drawList->AddCircleFilled(nodeCenter, nodeRadius, IM_COL32(70, 130, 200, 255));
		drawList->AddCircle(nodeCenter, nodeRadius, IM_COL32(255, 255, 255, 255), 0, 2.0f);

		std::string label = person.primaryFullText();
		ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
		ImVec2 textPos(nodeCenter.x - textSize.x * 0.5f, nodeCenter.y + nodeRadius + 4.0f);
		drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), label.c_str());
	}

	ImGui::End();
}

void Window::endFrame() {
	ImGui::Render();
	SDL_SetRenderDrawColor(renderer_, 30, 30, 30, 255);
	SDL_RenderClear(renderer_);
	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
	SDL_RenderPresent(renderer_);
}
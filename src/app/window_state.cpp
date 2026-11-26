#include <algorithm>

#include <nlohmann/json.hpp>

#include "window_state.h"

namespace {
	long long overlapArea(const IntRect& a, const IntRect& b) {
		const long long left = std::max<long long>(a.x, b.x);
		const long long top = std::max<long long>(a.y, b.y);
		const long long right = std::min<long long>(static_cast<long long>(a.x) + a.w, static_cast<long long>(b.x) + b.w);
		const long long bottom = std::min<long long>(static_cast<long long> (a.y) + a.h, static_cast<long long>(b.y) + b.h);
		if (right <= left || bottom <= top) return 0;
		return (right - left) * (bottom - top);
	}
}

std::string serializeWindowState(const WindowState& state) {
	nlohmann::json j;
	j["x"] = state.rect.x;
	j["y"] = state.rect.y;
	j["w"] = state.rect.w;
	j["h"] = state.rect.h;
	j["maximized"] = state.maximized;
	return j.dump(2);
}

WindowState deserializeWindowState(const std::string& jsonText) {
	WindowState state;  // valid = false unless everything below checks out
	try {
		const nlohmann::json j = nlohmann::json::parse(jsonText);
		if (!j.is_object()) return state;
		WindowState parsed;
		parsed.rect.x = j.at("x").get<int>();
		parsed.rect.y = j.at("y").get<int>();
		parsed.rect.w = j.at("w").get<int>();
		parsed.rect.h = j.at("h").get<int>();
		parsed.maximized = j.value("maximized", false);
		parsed.valid = parsed.rect.w > 0 && parsed.rect.h > 0;
		if (parsed.valid) state = parsed;
	}
	catch (const std::exception&) {
		// bad or missing file: fall through with the defaults
	}
	return state;
}

IntRect fitRectToDisplays(const IntRect& saved, const std::vector<IntRect>& displays, int minW, int minH) {
	if (displays.empty()) return saved;

	// The display that holds most of the window; ties fo to the earlier one (the primary).
	const IntRect* best = &displays[0];
	long long bestArea = overlapArea(saved, displays[0]);
	for (size_t i = 1; i < displays.size(); ++i) {
		const long long area = overlapArea(saved, displays[i]);
		if (area > bestArea) {
			best = &displays[i];
			bestArea = area;
		}
	}

	IntRect r = saved;
	r.w = std::clamp(r.w, std::min(minW, best->w), best->w);
	r.h = std::clamp(r.h, std::min(minH, best->h), best->h);

	if (bestArea == 0) {  // its monitor is gone: center on the first display
		r.x = best->x + (best->w - r.w) / 2;
		r.y = best->y + (best->h - r.h) / 2;
	}
	else {
		r.x = std::clamp(r.x, best->x, best->x + best->w - r.w);
		r.y = std::clamp(r.y, best->y, best->y + best->h - r.h);
	}

	return r;
}
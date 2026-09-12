#pragma once

#include <imgui.h>

// Converts between "world" coordinates (where notes conceptually live,
// independent of any view) and "screen" coordinates (actual pixels on the
// canvas). Physics/layout code always works in world space; only drawing
// code touches screen space, via this class.
class Camera {
public:
	void setViewport(ImVec2 origin, ImVec2 size);

	ImVec2 worldToScreen(ImVec2 worldPos) const;
	ImVec2 screenToWorld(ImVec2 screenPos) const;

	void pan(ImVec2 screenDelta);
	void zoomAt(ImVec2 screenPos, float zoomFactor);  // e.g. 1.1 = zoom in 10%
	void refitToBounds(ImVec2 minBounds, ImVec2 maxBounds, float marginPixels = 80.0f);

	float zoom() const { return zoom_; }
	ImVec2 offset() const { return offset_; }

	// Directly reestores a saved view - bypasses zoom-toward-cursor math,
	// since loading a project should snap to the saved state instantly,
	// not animate toward it the way interactive zoomAt() does.
	void setState(ImVec2 offset, float zoom);

private:
	ImVec2 viewportOrigin_{ 0, 0 };
	ImVec2 viewportSize_{ 0, 0 };

	ImVec2 offset_{ 0, 0 };  // world-space point currently at the viewport's center
	float zoom_ = 1.0f;
};
#include <algorithm>
#include <cmath>

#include "camera.h"

void Camera::setViewport(ImVec2 origin, ImVec2 size) {
	viewportOrigin_ = origin;
	viewportSize_ = size;
}

ImVec2 Camera::worldToScreen(ImVec2 worldPos) const {
	ImVec2 center(viewportOrigin_.x + viewportSize_.x * 0.5f,
		viewportOrigin_.y + viewportSize_.y * 0.5f);

	return ImVec2(
		center.x + (worldPos.x - offset_.x) * zoom_,
		center.y + (worldPos.y - offset_.y) * zoom_
	);
}

ImVec2 Camera::screenToWorld(ImVec2 screenPos) const {
	ImVec2 center(viewportOrigin_.x + viewportSize_.x * 0.5f,
		viewportOrigin_.y + viewportSize_.y * 0.5f);

	return ImVec2(
		offset_.x + (screenPos.x - center.x) / zoom_,
		offset_.y + (screenPos.y - center.y) / zoom_
	);
}

void Camera::pan(ImVec2 screenDelta) {
	// Moving the view by screenDelta pixels mean the world point under the
	// cursor should shift by that many screen pixels too, so convert the
	// delta into world units before applying it.
	offset_.x -= screenDelta.x / zoom_;
	offset_.y -= screenDelta.y / zoom_;
}

void Camera::zoomAt(ImVec2 screenPos, float zoomFactor) {
	// Zoom-to-cursor: remember which world point is under the cursor before
	// changing zoom, then adjust offset so that same world point stays under
	// the cursor after zoom changed - otherwise zooming would feel like it's
	// dragging the view around rather than zooming into where you're pointing.
	ImVec2 worldBefore = screenToWorld(screenPos);

	zoom_ = std::clamp(zoom_ * zoomFactor, 0.05f, 5.0f);

	ImVec2 worldAfter = screenToWorld(screenPos);
	offset_.x = worldBefore.x - worldAfter.x;
	offset_.y = worldBefore.y - worldAfter.y;
}

void Camera::refitToBounds(ImVec2 minBounds, ImVec2 maxBounds, float marginPixels) {
	offset_.x = (minBounds.x + maxBounds.x) * 0.5f;
	offset_.y = (minBounds.y + maxBounds.y) * 0.5f;

	float contentWidth = std::max(maxBounds.x - minBounds.x, 1.0f);
	float contentHeight = std::max(maxBounds.y - minBounds.y, 1.0f);

	float availableWidth = std::max(viewportSize_.x - marginPixels * 2.0f, 1.0f);
	float availableHeight = std::max(viewportSize_.x - marginPixels * 2.0f, 1.0f);

	float fitZoom = std::min(availableWidth / contentWidth, availableHeight / contentHeight);
	zoom_ = std::clamp(fitZoom, 0.1f, 1.5f);
}

void Camera::setState(ImVec2 offset, float zoom) {
	offset_ = offset;
	zoom_ = std::clamp(zoom, 0.05f, 5.0f);
}
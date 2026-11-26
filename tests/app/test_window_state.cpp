#include <doctest/doctest.h>

#include "window_state.h"

namespace {
	const std::vector<IntRect> kOneScreen = { { 0, 0, 1920, 1080 } };
	const std::vector<IntRect> kTwoScreens = { { 0, 0, 1920, 1080 },{ 1920, 0, 2560, 1440 } };
}

TEST_CASE("fitRectToDisplays leaves a rectangle that already fits") {
	IntRect r = fitRectToDisplays({ 100, 100, 800, 600 }, kOneScreen, 720, 420);
	CHECK(r.x == 100);
	CHECK(r.y == 100);
	CHECK(r.w == 800);
	CHECK(r.h == 600);
}

TEST_CASE("fitRectToDisplays pulls a rectangle that hangs over the edge back inside") {
	IntRect r = fitRectToDisplays({ 1700, 900, 800, 600 }, kOneScreen, 720, 420);
	CHECK(r.x == 1120);
	CHECK(r.y == 480);
	CHECK(r.w == 800);
	CHECK(r.h == 600);
}

TEST_CASE("fitRectToDisplays centers a window whose monitor is gone") {
	// saved on the second monitor, only the first one is connected now
	IntRect r = fitRectToDisplays({ 2500, 200, 800, 600 }, kOneScreen, 720, 420);
	CHECK(r.x == 560);
	CHECK(r.y == 240);
	CHECK(r.w == 800);
	CHECK(r.h == 600);
}

TEST_CASE("fitRectToDisplays keeps a window on the second monitor when it still exists") {
	IntRect r = fitRectToDisplays({ 2500, 200, 800, 600 }, kTwoScreens, 720, 420);
	CHECK(r.x == 2500);
	CHECK(r.y == 200);
}

TEST_CASE("fitRectToDisplays picks the display holding most of the window") {
	// 100px on the first screen, 700px on the second
	IntRect r = fitRectToDisplays({ 1820, 100, 800, 600 }, kTwoScreens, 720, 420);
	CHECK(r.x == 1920);
}

TEST_CASE("fitRectToDisplays shrinks a window biger then the display") {
	IntRect r = fitRectToDisplays({ -50, -50, 5000, 3000 }, kOneScreen, 720, 420);
	CHECK(r.x == 0);
	CHECK(r.y == 0);
	CHECK(r.w == 1920);
	CHECK(r.h == 1080);
}

TEST_CASE("fitRectToDisplays enlarges a window below the minimum") {
	IntRect r = fitRectToDisplays({ 100, 100, 200, 100 }, kOneScreen, 720, 420);
	CHECK(r.w == 720);
	CHECK(r.h == 420);
}

TEST_CASE("fitRectToDisplays with no displays returns the rectangle unchanged") {
	IntRect r = fitRectToDisplays({ -9000, 40, 800, 600 }, {}, 720, 420);
	CHECK(r.x == -9000);
	CHECK(r.w == 800);
}

TEST_CASE("window state round trips through JSON") {
	WindowState in;
	in.valid = true;
	in.rect = { 10, 20, 1000, 700 };
	in.maximized = true;

	WindowState out = deserializeWindowState(serializeWindowState(in));
	CHECK(out.valid);
	CHECK(out.rect.x == 10);
	CHECK(out.rect.y == 20);
	CHECK(out.rect.w == 1000);
	CHECK(out.rect.h == 700);
	CHECK(out.maximized);
}

TEST_CASE("window state ignore broken or useless files") {
	CHECK_FALSE(deserializeWindowState("").valid);
	CHECK_FALSE(deserializeWindowState("not json").valid);
	CHECK_FALSE(deserializeWindowState("[1,2,3]").valid);
	CHECK_FALSE(deserializeWindowState("{\"x\":0,\"y\":0}").valid);
	CHECK_FALSE(deserializeWindowState("{\"x\":0,\"y\":0,\"w\":0,\"h\":500}").valid);
	CHECK_FALSE(deserializeWindowState("{\"x\":\"a\",\"y\":0,\"w\":500,\"h\":500}").valid);
}
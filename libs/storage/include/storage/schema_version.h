#pragma once

namespace storage {
	// The layout this build creates
	constexpr int kSchemaVersion = 9;

	// The oldest layout this build can open or upgrade from
	constexpr int kMinSupportedVersion = 9;
}
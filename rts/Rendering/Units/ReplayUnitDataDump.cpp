#pragma once

#include <deque>
#include <pair>

MatchUnitPosition {
};

using UnitPosition = std::pair<float, float>;

struct MatchFrame {
	std::vector<UnitPosition>;
};

struct MatchData {

	size_t sampleDelayMs = 1000;
	spring_t lastUpdate = 0;
	std::deque<MatchFrame> frames;

	bool create_frame() {
		if ((spring_gettime() - lastUpdate).toMilliSecsi() > sampleDelayMs) {
			frames.push_back();
			return true;
		}
	}

	void add_pos(float x, float y) {
		frames.back().emplace_back({x,y});
	}
};

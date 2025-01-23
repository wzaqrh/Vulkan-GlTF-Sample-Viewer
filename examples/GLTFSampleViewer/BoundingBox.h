#pragma once
#include "gltfShaderStruct.h"

class BoundingBox {
public:
	glm::vec3 min;
	glm::vec3 max;

	// Constructor: Initializes an invalid bounding box
	BoundingBox() {
		reset();
	}
	BoundingBox(glm::vec3 min, glm::vec3 max) {
		this->min = min;
		this->max = max;
	}

	// Resets the bounding box to an invalid state
	void reset() {
		min = glm::vec3(std::numeric_limits<float>::max());
		max = glm::vec3(std::numeric_limits<float>::lowest());
	}

	// Checks if the bounding box is valid
	bool isValid() const { return (min.x <= max.x && min.y <= max.y && min.z <= max.z); }
	operator bool() const { return isValid(); }

	// Merges the current bounding box with another bounding box
	void merge(const BoundingBox& other) {
		if (!other.isValid()) return;

		min = glm::min(min, other.min);
		max = glm::max(max, other.max);
	}

	// Merges the current bounding box with a single point
	void merge(const glm::vec3& point) {
		min = glm::min(min, point);
		max = glm::max(max, point);
	}

	// Checks if a point is inside the bounding box
	bool contains(const glm::vec3& point) const {
		return (point.x >= min.x && point.x <= max.x &&
			point.y >= min.y && point.y <= max.y &&
			point.z >= min.z && point.z <= max.z);
	}

	// Computes the size (extent) of the bounding box
	glm::vec3 size() const {
		return isValid() ? (max - min) : glm::vec3(0.0f);
	}

	// Computes the center of the bounding box
	glm::vec3 center() const {
		return isValid() ? (min + max) * 0.5f : glm::vec3(0.0f);
	}

	// Transforms the bounding box by a matrix
	BoundingBox transform(const glm::mat4& worldMat) const {
		if (!isValid()) {
			return BoundingBox(); // Return an invalid bounding box
		}

		// Define the 8 corners of the bounding box
		std::vector<glm::vec3> corners = {
			{min.x, min.y, min.z},
			{max.x, min.y, min.z},
			{min.x, max.y, min.z},
			{min.x, min.y, max.z},
			{max.x, max.y, min.z},
			{max.x, min.y, max.z},
			{min.x, max.y, max.z},
			{max.x, max.y, max.z}
		};

		// Transform all corners and compute the new bounding box
		BoundingBox transformedBBox;
		for (const auto& corner : corners) {
			glm::vec4 transformedCorner = worldMat * glm::vec4(corner, 1.0f);
			transformedBBox.merge(glm::vec3(transformedCorner));
		}

		return transformedBBox;
	}
};
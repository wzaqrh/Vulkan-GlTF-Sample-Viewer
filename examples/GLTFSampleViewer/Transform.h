#pragma once
#include "gltfShaderStruct.h"

struct Transform
{
	Transform() {
		scale = glm::vec3(1, 1, 1);
	}
	Transform(glm::mat4 m) {
		matrix = m;
		decomposeMatrix(matrix, translation, rotation, scale);
	}
	Transform(glm::vec3 t, glm::quat q, glm::vec3 s = glm::vec3(1, 1, 1)) {
		translation = t;
		scale = s;
		rotation = q;
		matrix = composeMatrix(translation, rotation, scale);
	}

	static void decomposeMatrix(const glm::mat4& matrix, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale) {
		translation = glm::vec3(matrix[3][0], matrix[3][1], matrix[3][2]);

		scale = glm::vec3(
			glm::length(glm::vec3(matrix[0][0], matrix[0][1], matrix[0][2])),
			glm::length(glm::vec3(matrix[1][0], matrix[1][1], matrix[1][2])),
			glm::length(glm::vec3(matrix[2][0], matrix[2][1], matrix[2][2]))
		);

		glm::mat3 rotationMatrix;
		rotationMatrix[0] = glm::vec3(matrix[0]) / scale.x;
		rotationMatrix[1] = glm::vec3(matrix[1]) / scale.y;
		rotationMatrix[2] = glm::vec3(matrix[2]) / scale.z;
		rotation = glm::quat_cast(rotationMatrix);
	}
	static glm::mat4 composeMatrix(glm::vec3 translation, glm::quat rotation, glm::vec3 scale) {
		glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);
		glm::mat4 rotationMatrix = glm::mat4_cast(rotation);
		glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), translation);
		return translationMatrix * rotationMatrix * scaleMatrix;
	}

	void setTranslation(glm::vec3 t) {
		translation = t;
		matrixDirty = true;
	}
	void setScale(glm::vec3 s) {
		scale = s;
		matrixDirty = true;
	}
	void setRotation(glm::quat q) {
		rotation = q;
		matrixDirty = true;
	}
	const glm::mat4& getMatrix() const {
		if (matrixDirty) {
			matrixDirty = false;
			matrix = composeMatrix(translation, rotation, scale);
		}
		return matrix;
	}

	glm::vec3 translation;
	glm::vec3 scale;
	glm::quat rotation;
private:
	mutable bool matrixDirty = false;
	mutable glm::mat4 matrix;
};


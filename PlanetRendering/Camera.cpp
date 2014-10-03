#include "Camera.h"

Camera::Camera(int windowWidth, int windowHeight) : 
	Position(1.1f, 0.0f, 0.0f),
	aspectRatio(((float)windowWidth)/(float)windowHeight),
	FieldOfView(85.0f), XRotation(0.0f), ZRotation(0.0f),
	NEAR_PLANE(0.1f), FAR_PLANE(200.0f)
{
    aspectRatio = ((float)windowWidth)/(float)windowHeight;
}

Camera::~Camera() {}

glm::mat4 Camera::GetViewMatrix()
{
	return glm::eulerAngleXZ(XRotation, ZRotation) * glm::translate(Position) * glm::scale(glm::vec3(1.0f, 1.0f, 1.0f));
}

void Camera::ResizedWindow(int windowWidth, int windowHeight)
{
	//aspectRatio = ((float)windowWidth)/((float)windowHeight);
}

glm::mat4 Camera::GetProjectionMatrix()
{
	return glm::perspective(
		FieldOfView,
		1.333333f,
		0.00001f,  //near clipping plane
		1000.0f //far clipping plane
	);
}
glm::mat4 Camera::GetTransformMatrix()
{
	return GetProjectionMatrix() * GetViewMatrix();
}

glm::vec3 Camera::GetViewDirection()
{
	return glm::vec3(glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) * glm::eulerAngleXZ(XRotation, ZRotation));
}

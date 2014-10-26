#include "Camera.h"

Camera::Camera(int windowWidth, int windowHeight) : 
	position(20.1f, 10.0f, 10.0f),
	aspectRatio(((float)windowWidth)/(float)windowHeight),
	FieldOfView(60.), XRotation(0.0f), ZRotation(90.0f), YRotation(0.0f),PlanetRotation(0.0),
NEAR_PLANE(std::numeric_limits<float>::epsilon()),
FAR_PLANE(200.0f)
{
}

Camera::~Camera() {}

vmat4 Camera::GetViewMatrix()
{
    return glm::eulerAngleXZ(XRotation, ZRotation) * glm::translate(-position) * glm::rotate(vmat4(), PlanetRotation, vvec3(0.0,0.0,1.0));
}

void Camera::ResizedWindow(int windowWidth, int windowHeight)
{
	//aspectRatio = ((float)windowWidth)/((float)windowHeight);
}

vmat4 Camera::GetProjectionMatrix()
{
	return glm::perspective(
		FieldOfView,
		aspectRatio,
		NEAR_PLANE,  //near clipping plane
		FAR_PLANE //far clipping plane
	);
}

vmat4 Camera::GetTransformMatrix()
{
    return glm::rotate(GetProjectionMatrix() , static_cast<vfloat>(0.0), vvec3(0,0,1))* GetViewMatrix();
}

vvec3 Camera::GetViewDirection()
{
    return vvec3(vvec4(0.0, 0.0, 1.0, 1.0) * glm::eulerAngleXZ(XRotation, ZRotation));
}

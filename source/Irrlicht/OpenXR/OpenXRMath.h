#pragma once

#include "OpenXRHeaders.h"

// Multiply quaternions
static inline XrQuaternionf quatMul(const XrQuaternionf& a, const XrQuaternionf& b)
{
        XrQuaternionf result;
        result.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
        result.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
        result.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
        result.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
        return result;
}

// Invert a quaternion
static inline XrQuaternionf quatInv(const XrQuaternionf& a)
{
	XrQuaternionf result = {
		.x = -a.x,
		.y = -a.y,
		.z = -a.z,
		.w = a.w,
	};
	return result;
}

// Apply quaternion 'a' as a rotation to vector 'b'
static inline XrVector3f quatApply(const XrQuaternionf& a, const XrVector3f &b)
{
        XrQuaternionf bquat = {
		.x = b.x,
		.y = b.y,
		.z = b.z,
		.w = 0,
	};
	XrQuaternionf r = quatMul(quatMul(a, bquat), quatInv(a));
	XrVector3f result = {
		.x = r.x,
		.y = r.y,
		.z = r.z,
	};
	return result;
}

static inline XrVector3f vecAdd(const XrVector3f& a, const XrVector3f& b)
{
	XrVector3f result = {
		.x = a.x + b.x,
		.y = a.y + b.y,
		.z = a.z + b.z,
	};
	return result;
}

static inline XrVector3f vecSub(const XrVector3f& a, const XrVector3f& b)
{
	XrVector3f result = {
		.x = a.x - b.x,
		.y = a.y - b.y,
		.z = a.z - b.z,
	};
	return result;
}

static inline float vecLengthSq(const XrVector3f& a)
{
	return a.x * a.x + a.y * a.y + a.z * a.z;
}

static inline float vecLength(const XrVector3f& a)
{
	return sqrt(vecLengthSq(a));
}

static inline XrQuaternionf quatNormalize(const XrQuaternionf& a)
{
	float invlen = 1.0 / sqrt(a.x * a.x + a.y * a.y + a.z * a.z + a.w * a.w);
	XrQuaternionf result;
	result.x = a.x * invlen;
	result.y = a.y * invlen;
	result.z = a.z * invlen;
	result.w = a.w * invlen;
	return result;
}

// Compose two poses
static inline XrPosef poseMul(const XrPosef& a, const XrPosef& b)
{
        XrPosef result;
        result.orientation = quatNormalize(quatMul(a.orientation, b.orientation));
        result.position = vecAdd(a.position, quatApply(a.orientation, b.position));
	return result;
}

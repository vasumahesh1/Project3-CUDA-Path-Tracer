#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/intersect.hpp>

#include "sceneStructs.h"
#include "utilities.h"
#include "warpfunctions.h"

#define ENABLE_MESH_BOUNDING_CULL

#define KD_TREE_QUEUE_SIZE 64

#define USE_SPHERE_LIGHTS

/**
 * Handy-dandy hash function that provides seeds for random number generation.
 */
__host__ __device__ inline unsigned int utilhash(unsigned int a)
{
  a = (a + 0x7ed55d16) + (a << 12);
  a = (a ^ 0xc761c23c) ^ (a >> 19);
  a = (a + 0x165667b1) + (a << 5);
  a = (a + 0xd3a2646c) ^ (a << 9);
  a = (a + 0xfd7046c5) + (a << 3);
  a = (a ^ 0xb55a4f09) ^ (a >> 16);
  return a;
}

__device__ inline void CoordinateSystem(const Vector3f& v1, Vector3f* v2, Vector3f* v3)
{
  if (std::abs(v1.x) > std::abs(v1.y))
    *v2 = Vector3f(-v1.z, 0, v1.x) / std::sqrt(v1.x * v1.x + v1.z * v1.z);
  else
    *v2 = Vector3f(0, v1.z, -v1.y) / std::sqrt(v1.y * v1.y + v1.z * v1.z);
  *v3 = glm::cross(v1, *v2);
}

// CHECKITOUT
/**
 * Compute a point at parameter value `t` on ray `r`.
 * Falls slightly short so that it doesn't intersect the object it's hitting.
 */
__host__ __device__ glm::vec3 getPointOnRay(Ray r, float t)
{
  return r.origin + (t - .0001f) * glm::normalize(r.direction);
}

/**
 * Multiplies a mat4 and a vec4 and returns a vec3 clipped from the vec4.
 */
__host__ __device__ glm::vec3 multiplyMV(glm::mat4 m, glm::vec4 v)
{
  return glm::vec3(m * v);
}

// Returns +/- [0, 2]
__host__ __device__ int GetFaceIndex(const Point3f& P)
{
  int idx = 0;
  float val = -1;
  for (int i = 0; i < 3; i++)
  {
    if (glm::abs(P[i]) > val)
    {
      idx = int(i * glm::sign(P[i]));
      val = glm::abs(P[i]);
    }
  }
  return idx;
}

__host__ __device__ Normal3f GetCubeNormal(const Point3f& P)
{
  int idx = glm::abs(GetFaceIndex(Point3f(P)));
  Normal3f N(0, 0, 0);
  N[idx] = glm::sign(P[idx]);
  return N;
}

__host__ __device__ Normal3f GetCubeTangent(const Point3f& P)
{
  int idx = glm::abs(GetFaceIndex(Point3f(P)));

  Normal3f T;
  float direction = glm::sign(P[idx]);

  switch (idx)
  {
    // Z Faces
  case 2:
    T = Normal3f(1, 0, 0) * direction;
    break;

    // X Faces
  case 0:
    T = Normal3f(0, 0, 1) * -direction;
    break;

    // Y Faces
  default:
    T = Normal3f(1, 0, 0) * direction;
    break;
  }

  return T;
}

__host__ __device__ inline float boxIntersectionTest(Geom box, Ray r,
                                                     glm::vec3& intersectionPoint, glm::vec3& normal,
                                                     glm::vec3& bitangent,
                                                     glm::vec3& tangent)
{
  const Ray r_loc{
    multiplyMV(box.inverseTransform, glm::vec4(r.origin, 1.0f)),
    glm::normalize(multiplyMV(box.inverseTransform, glm::vec4(r.direction, 0.0f)))
  };

  float t_n = -1000000;
  float t_f = 1000000;

  for (int i = 0; i < 3; i++)
  {
    //Ray parallel to slab check
    if (r_loc.direction[i] == 0)
    {
      if (r_loc.origin[i] < -0.5f || r_loc.origin[i] > 0.5f)
      {
        return -1.0f;
      }
    }

    //If not parallel, do slab intersect check
    float t0 = (-0.5f - r_loc.origin[i]) / r_loc.direction[i];
    float t1 = (0.5f - r_loc.origin[i]) / r_loc.direction[i];
    if (t0 > t1)
    {
      const float temp = t1;
      t1 = t0;
      t0 = temp;
    }
    if (t0 > t_n)
    {
      t_n = t0;
    }
    if (t1 < t_f)
    {
      t_f = t1;
    }
  }

  if (t_n < t_f)
  {
    const float t = t_n > 0 ? t_n : t_f;
    if (t < 0)
    {
      return -1.0f;
    }

    //Lastly, transform the point found in object space by T
    const glm::vec3 P = glm::vec3(r_loc.origin + t * r_loc.direction);
    intersectionPoint = multiplyMV(box.transform, glm::vec4(P, 1.f));

    const Vector3f norm = glm::normalize(GetCubeNormal(P));
    const Vector3f tang = glm::normalize(GetCubeTangent(P));
    const Vector3f bitan = glm::normalize(glm::cross(norm, tang));

    normal = glm::normalize(glm::vec3(box.invTranspose * glm::vec4(norm, 0)));
    tangent = glm::normalize(glm::mat3(box.transform) * tang);
    bitangent = glm::normalize(glm::mat3(box.transform) * bitan);

    return glm::length(r.origin - intersectionPoint);
  }

  //If t_near was greater than t_far, we did not hit the cube
  return -1.0f;
}


__host__ __device__ inline float boundingBoxIntersectionTest(glm::vec3 min, glm::vec3 max, Ray r_loc)
{
  float t_n = -1000000;
  float t_f = 1000000;

  for (int i = 0; i < 3; i++)
  {
    //Ray parallel to slab check
    if (r_loc.direction[i] == 0)
    {
      if (r_loc.origin[i] < min[i] || r_loc.origin[i] > max[i])
      {
        return -1.0f;
      }
    }

    //If not parallel, do slab intersect check
    float t0 = (min[i] - r_loc.origin[i]) / r_loc.direction[i];
    float t1 = (max[i] - r_loc.origin[i]) / r_loc.direction[i];
    if (t0 > t1)
    {
      const float temp = t1;
      t1 = t0;
      t0 = temp;
    }
    if (t0 > t_n)
    {
      t_n = t0;
    }
    if (t1 < t_f)
    {
      t_f = t1;
    }
  }

  if (t_n < t_f)
  {
    float t = t_n > 0 ? t_n : t_f;
    if (t < 0)
    {
      return -1.0f;
    }

    //Lastly, transform the point found in object space by T
    glm::vec3 P = glm::vec3(r_loc.origin + t * r_loc.direction);
    return glm::length(r_loc.origin - P);
  }

  //If t_near was greater than t_far, we did not hit the cube
  return -1.0f;
}

__host__ __device__ inline float sphereIntersectionTest(Geom sphere, Ray r,
                                                        glm::vec3& intersectionPoint, glm::vec3& normal,
                                                        glm::vec3& bitangent,
                                                        glm::vec3& tangent)
{
  //Transform the ray
  const Ray rt{
    multiplyMV(sphere.inverseTransform, glm::vec4(r.origin, 1.0f)),
    glm::normalize(multiplyMV(sphere.inverseTransform, glm::vec4(r.direction, 0.0f)))
  };

  const float A = pow(rt.direction.x, 2.f) + pow(rt.direction.y, 2.f) + pow(rt.direction.z, 2.f);
  const float B = 2 * (rt.direction.x * rt.origin.x + rt.direction.y * rt.origin.y + rt.direction.z * rt.origin.z);
  const float C = pow(rt.origin.x, 2.f) + pow(rt.origin.y, 2.f) + pow(rt.origin.z, 2.f) - 1.f; //Radius is 1.f
  const float discriminant = B * B - 4 * A * C;

  //If the discriminant is negative, then there is no real root
  if (discriminant < 0)
  {
    return -1.0f;
  }

  float t = (-B - sqrt(discriminant)) / (2 * A);

  if (t < 0)
  {
    t = (-B + sqrt(discriminant)) / (2 * A);
  }

  if (t >= 0)
  {
    const glm::vec3 objspaceIntersection = getPointOnRay(rt, t);
    intersectionPoint = multiplyMV(sphere.transform, glm::vec4(objspaceIntersection, 1.f));
    normal = glm::normalize(multiplyMV(sphere.invTranspose, glm::vec4(objspaceIntersection, 0.f)));

    tangent = glm::normalize(
      glm::mat3(sphere.transform) * glm::cross(Vector3f(0, 1, 0), (glm::normalize(objspaceIntersection))));
    bitangent = glm::normalize(glm::cross(normal, tangent));

    return glm::length(r.origin - intersectionPoint);
  }

  return -1.0f;
}


namespace Shapes
{
  namespace Triangles
  {
    __host__ __device__ inline float TriArea(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3)
    {
      return glm::length(glm::cross(p1 - p2, p3 - p2)) * 0.5f;
    }

    __host__ __device__ inline float IntersectSingle(const Geom& shape, const Triangle& tri, const Ray& r,
                                                     glm::vec3& localIntersection, glm::vec3& normal, glm::vec2& uv)
    {
      // const bool result = glm::intersectRayTriangle(r.origin, r.direction, tri.p1, tri.p2, tri.p3, localIntersection);
      //
      // if (result)
      // {
      //   return glm::length(r.origin - localIntersection);
      // }
      //
      // return -1.0f;

      float t = glm::dot(tri.planeNormal, (tri.p1 - r.origin)) / glm::dot(tri.planeNormal, r.direction);
      if (t < 0) { return -1.0f; }

      if (glm::dot(tri.planeNormal, r.direction) > 0.0f)
        return -1.0f;

      glm::vec3 P = r.origin + t * r.direction;
      // Barycentric test
      float A = 0.5f * glm::length(glm::cross(tri.p1 - tri.p2, tri.p1 - tri.p3));
      float A0 = 0.5f * glm::length(glm::cross(P - tri.p2, P - tri.p3));
      float A1 = 0.5f * glm::length(glm::cross(P - tri.p3, P - tri.p1));
      float A2 = 0.5f * glm::length(glm::cross(P - tri.p1, P - tri.p2));
      float sum = A0 + A1 + A2;

      if (sum <= A * (1.0f + EPSILON))
      {
        localIntersection = P;

        normal = glm::normalize(tri.n1 * A0 / A + tri.n2 * A1 / A + tri.n3 * A2 / A);
        uv = (tri.uv1 * A0 / A) + (tri.uv2 * A1 / A) + (tri.uv3 * A2 / A);

        return t;
      }

      return -1.0f;
    }

    __host__ __device__ inline Normal3f GetNormal(const Triangle& tri, const Point3f& P)
    {
      const float A = TriArea(tri.p1, tri.p2, tri.p3);
      const float A0 = TriArea(tri.p2, tri.p3, P);
      const float A1 = TriArea(tri.p1, tri.p3, P);
      const float A2 = TriArea(tri.p1, tri.p2, P);
      return glm::normalize(tri.n1 * A0 / A + tri.n2 * A1 / A + tri.n3 * A2 / A);
    }

    __host__ __device__ inline Vector2f GetUV(const Triangle& tri, const Point3f& P)
    {
      const float A = TriArea(tri.p1, tri.p2, tri.p3);
      const float A0 = TriArea(tri.p2, tri.p3, P);
      const float A1 = TriArea(tri.p1, tri.p3, P);
      const float A2 = TriArea(tri.p1, tri.p2, P);
      return (tri.uv1 * A0 / A) + (tri.uv2 * A1 / A) + (tri.uv3 * A2 / A);
    }

    __host__ __device__ inline float BulkIntersect(const Geom& shape, Triangle* triangles, const Ray& originalRay,
                                                   glm::vec3& intersectionPoint,
                                                   glm::vec3& normal, glm::vec3& bitangent, glm::vec3& tangent,
                                                   glm::vec2& uv)
    {
      const Ray r{
        multiplyMV(shape.inverseTransform, glm::vec4(originalRay.origin, 1.0f)),
        glm::normalize(multiplyMV(shape.inverseTransform, glm::vec4(originalRay.direction, 0.0f)))
      };

      bool found = false;
      float minDist = FLT_MAX;
      int closestTriIndex = -1;
      glm::vec3 P;

      glm::vec3 tmpNormal;
      glm::vec2 tmpUv;

      glm::vec3 selPoint;
      glm::vec3 selNormal;
      glm::vec2 selUv;

      for (int idx = 0; idx < shape.numTriangles; ++idx)
      {
        const Triangle& tri = triangles[shape.meshStartIndex + idx];
        const float dist = IntersectSingle(shape, tri, r, P, tmpNormal, tmpUv);

        //Check that P is within the bounds of the square
        if (dist > EPSILON && dist < minDist)
        {
          found = true;
          minDist = dist;
          selPoint = P;
          selNormal = tmpNormal;
          selUv = tmpUv;
          closestTriIndex = shape.meshStartIndex + idx;
        }
      }

      if (found)
      {
        const Triangle& chosenOne = triangles[closestTriIndex];
        intersectionPoint = multiplyMV(shape.transform, glm::vec4(selPoint, 1.f));

        const Point2f deltaUV1 = chosenOne.uv2 - chosenOne.uv1;
        const Point2f deltaUV2 = chosenOne.uv3 - chosenOne.uv1;

        const Vector3f deltaPos1 = chosenOne.p2 - chosenOne.p1;
        const Vector3f deltaPos2 = chosenOne.p3 - chosenOne.p1;

        const float result = deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x;

        glm::vec3 tan;
        glm::vec3 bit;

        if (result != 0.0f)
        {
          float r = 1.0f / (result);
          tan = glm::normalize(glm::vec3((deltaUV2.y * deltaPos1.x - deltaUV1.y * deltaPos2.x) * r,
                                         (deltaUV2.y * deltaPos1.y - deltaUV1.y * deltaPos2.y) * r,
                                         (deltaUV2.y * deltaPos1.z - deltaUV1.y * deltaPos2.z) * r));
          bit = glm::normalize(glm::cross(selNormal, tan));
          tan = glm::normalize(glm::cross(bit, selNormal));
        }
        else
        {
          tan = glm::vec3(1.0f, 0.0f, 0.0f);
          bit = glm::normalize(glm::cross(selNormal, tan));
          tan = glm::normalize(glm::cross(bit, selNormal));
        }

        normal = glm::normalize(glm::mat3(shape.invTranspose) * selNormal);
        tangent = glm::normalize(glm::mat3(shape.transform) * tan);
        bitangent = glm::normalize(glm::mat3(shape.transform) * bit);

        uv = selUv;

        return glm::length(originalRay.origin - intersectionPoint);
      }

      return -1.0f;
    }

    __host__ __device__ inline float BufferIntersect(const Geom& shape, Triangle* triangles, int startIdx, int endIdx,
                                                     const Ray& originalRay, glm::vec3& intersectionPoint,
                                                     glm::vec3& normal, glm::vec3& bitangent, glm::vec3& tangent,
                                                     glm::vec2& uv)
    {
      Ray r;

      //Transform the ray - Overall transform on GEOM
      r.origin = multiplyMV(shape.inverseTransform, glm::vec4(originalRay.origin, 1.0f));
      r.direction = glm::normalize(multiplyMV(shape.inverseTransform, glm::vec4(originalRay.direction, 0.0f)));


      bool found = false;
      float minDist = FLT_MAX;
      int closestTriIndex = -1;
      glm::vec3 P;

      glm::vec3 tmp_normal;
      glm::vec2 tmp_uv;

      glm::vec3 sel_point;
      glm::vec3 sel_normal;
      glm::vec2 sel_uv;

      for (int idx = startIdx; idx <= endIdx; ++idx)
      {
        const Triangle& tri = triangles[idx];
        const float dist = IntersectSingle(shape, tri, r, P, tmp_normal, tmp_uv);

        //Check that P is within the bounds of the square
        if (dist > EPSILON && dist < minDist)
        {
          found = true;
          minDist = dist;
          sel_point = P;
          sel_normal = tmp_normal;
          sel_uv = tmp_uv;
          closestTriIndex = idx;
        }
      }

      if (found)
      {
        const Triangle& chosenOne = triangles[closestTriIndex];
        intersectionPoint = multiplyMV(shape.transform, glm::vec4(sel_point, 1.f));

        const Point2f deltaUV1 = chosenOne.uv2 - chosenOne.uv1;
        const Point2f deltaUV2 = chosenOne.uv3 - chosenOne.uv1;

        const Vector3f deltaPos1 = chosenOne.p2 - chosenOne.p1;
        const Vector3f deltaPos2 = chosenOne.p3 - chosenOne.p1;

        const float result = deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x;

        glm::vec3 tan;
        glm::vec3 bit;

        if (result != 0.0f)
        {
          float r = 1.0f / (result);
          tan = glm::normalize(glm::vec3((deltaUV2.y * deltaPos1.x - deltaUV1.y * deltaPos2.x) * r,
                                         (deltaUV2.y * deltaPos1.y - deltaUV1.y * deltaPos2.y) * r,
                                         (deltaUV2.y * deltaPos1.z - deltaUV1.y * deltaPos2.z) * r));
          bit = glm::normalize(glm::cross(sel_normal, tan));
          tan = glm::normalize(glm::cross(bit, sel_normal));
        }
        else
        {
          tan = glm::vec3(1.0f, 0.0f, 0.0f);
          bit = glm::normalize(glm::cross(sel_normal, tan));
          tan = glm::normalize(glm::cross(bit, sel_normal));
        }

        normal = glm::normalize(glm::mat3(shape.invTranspose) * sel_normal);
        tangent = glm::normalize(glm::mat3(shape.transform) * tan);
        bitangent = glm::normalize(glm::mat3(shape.transform) * bit);

        uv = sel_uv;

        return glm::length(originalRay.origin - intersectionPoint);
      }

      return -1.0f;
    }
  }

  namespace SquarePlane
  {
    __host__ __device__ inline float Intersect(const Geom& shape, const Ray& r, glm::vec3& intersectionPoint,
                                               glm::vec3& normal, glm::vec3& bitangent, glm::vec3& tangent,
                                               glm::vec2& uv)
    {
      //Transform the ray
      const glm::vec3 ro = multiplyMV(shape.inverseTransform, glm::vec4(r.origin, 1.0f));
      const glm::vec3 rd = glm::normalize(multiplyMV(shape.inverseTransform, glm::vec4(r.direction, 0.0f)));

      //Ray-plane intersection
      const float t = glm::dot(glm::vec3(0, 0, 1), (glm::vec3(0.5f, 0.5f, 0) - ro)) / glm::dot(glm::vec3(0, 0, 1), rd);
      const Point3f P = Point3f(t * rd + ro);

      //Check that P is within the bounds of the square
      if (t > EPSILON && P.x >= -0.5f && P.x <= 0.5f && P.y >= -0.5f && P.y <= 0.5f)
      {
        intersectionPoint = multiplyMV(shape.transform, glm::vec4(P, 1.f));

        normal = glm::normalize(glm::mat3(shape.invTranspose) * Normal3f(0, 0, 1));
        tangent = glm::normalize(glm::mat3(shape.transform) * Normal3f(1, 0, 0));
        bitangent = glm::normalize(glm::mat3(shape.transform) * Normal3f(0, 1, 0));

        uv = Point2f(P.x + 0.5f, P.y + 0.5f);

        return glm::length(r.origin - intersectionPoint);
      }
      return -1.0f;
    }

    __device__ inline float Area(Geom* geometry)
    {
      return geometry->scale[0] * geometry->scale[1];
    }

    __device__ inline void Sample(Geom* geometry, const float rngX, const float rngY, Float* pdf,
                                           Intersection* intr)
    {
      *pdf = 1.0f / (geometry->scale[0] * geometry->scale[1]);

      const Point3f localPoint = Point3f(Point2f(rngX, rngY) - Point2f(0.5f, 0.5f), 0.0f);
      const Point3f worldPoint = Point3f(geometry->transform * glm::vec4(localPoint, 1.0f));

      intr->point = worldPoint;
      intr->normal = glm::normalize(glm::mat3(geometry->inverseTransform) * Normal3f(0, 0, 1));
    }
  }

  namespace Sphere
  {
    __device__ inline  Intersection Sample(Geom* geometry, const float rngX, const float rngY, Float *pdf)
    {
      Point3f pObj = Warp::SquareToSphereUniform(rngX, rngY);

      Intersection it;
      it.normal = glm::normalize(multiplyMV(geometry->invTranspose, glm::vec4(pObj, 0.f)));
      it.point = Point3f(geometry->transform * glm::vec4(pObj, 1.0f));

      *pdf = 1.0f / (4.f * float(Pi) * geometry->scale.x * geometry->scale.y);

      return it;
    }

    __device__ inline Intersection Sample(Geom* geometry, const Point3f& refPoint, const Normal3f& refNormal, const float rngX,
      const float rngY, float* pdf)
    {
      const Point3f center = Point3f(geometry->transform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
      const Vector3f centerToRef = glm::normalize(center - refPoint);
      Vector3f tan, bit;

      CoordinateSystem(centerToRef, &tan, &bit);

      Point3f pOrigin;
      if(glm::dot(center - refPoint, refNormal) > 0)
        pOrigin = refPoint + refNormal * RayEpsilon;
      else
        pOrigin = refPoint - refNormal * RayEpsilon;

      if (glm::distance2(pOrigin, center) <= 1.f) {
        return Sample(geometry, rngX, rngY, pdf);
      }

      const float sinThetaMax2 = 1 / glm::distance2(refPoint, center); // Again, radius is 1
      const float cosThetaMax = std::sqrt(glm::max((float)0.0f, 1.0f - sinThetaMax2));
      const float cosTheta = (1.0f - rngX) +rngX * cosThetaMax;
      const float sinTheta = std::sqrt(glm::max((float)0, 1.0f- cosTheta * cosTheta));
      const float phi = rngY * 2.0f * Pi;

      const float dc = glm::distance(refPoint, center);
      const float ds = dc * cosTheta - glm::sqrt(glm::max((float)0.0f, 1 - dc * dc * sinTheta * sinTheta));

      const float cosAlpha = (dc * dc + 1 - ds * ds) / (2 * dc * 1);
      const float sinAlpha = glm::sqrt(glm::max((float)0.0f, 1.0f - cosAlpha * cosAlpha));

      const Vector3f nObj = sinAlpha * glm::cos(phi) * -tan + sinAlpha * glm::sin(phi) * -bit + cosAlpha * -centerToRef;
      const Point3f pObj = Point3f(nObj); // Would multiply by radius, but it is always 1 in object space

      Intersection intr;
      intr.point = Point3f(geometry->transform * glm::vec4(pObj.x, pObj.y, pObj.z, 1.0f));
      intr.normal = glm::normalize(multiplyMV(geometry->invTranspose, glm::vec4(nObj, 0.f)));;

      *pdf = 1.0f / (2.0f * float(Pi) * (1 - cosThetaMax));
      return intr;
    }
  }

  __device__ inline void Sample(Geom* geometry, const float rngX, const float rngY, float* pdf,
                                         Intersection* intr)
  {
    // Hardcoded Square
    SquarePlane::Sample(geometry, rngX, rngY, pdf, intr);
  }

  __device__ inline Intersection Sample(Geom* geometry, const Point3f& refPoint, const Normal3f& refNormal, const float rngX,
                                                 const float rngY, float* pdf)
  {
    Intersection isectLight;
#ifdef USE_SPHERE_LIGHTS
    if (geometry->type == SPHERE)
    {
      return Sphere::Sample(geometry, refPoint, refNormal, rngX, rngY, pdf);
    }
#endif

    Sample(geometry, rngX, rngY, pdf, &isectLight);

    const Vector3f wi = glm::normalize(isectLight.point - refPoint);

    const Vector3f toRef = refPoint - isectLight.point;
    const float distSq = glm::length2(toRef);

    const float dot = glm::abs(glm::dot(isectLight.normal, -wi));

    if (dot <= 0.00001)
    {
      *pdf = 0;
      return isectLight;
    }

    *pdf = (distSq) / (dot / (*pdf));

    return isectLight;
  }
}

__device__ void kdIntersectionTest(const int nodeIdx, const Ray& targetRay, const Ray& transformedRay, const Geom& geom,
                                   KDNode* kdNodes, Triangle* kdTriangles, glm::vec3& intersectionPoint,
                                   glm::vec3& normal, glm::vec3& bitangent,
                                   glm::vec3& tangent, glm::vec2& uv, float& minT)
{
  const KDNode currentNode = kdNodes[nodeIdx];
  const float boundT = boundingBoxIntersectionTest(currentNode.min, currentNode.max, transformedRay);

  if (boundT < EPSILON)
  {
    // Didn't intersect node's AABB
    return;
  }

  // Child Node
  if (currentNode.triStartIdx >= 0)
  {
    glm::vec3 tmp_intersect;
    glm::vec3 tmp_normal;
    glm::vec2 tmp_uv;
    glm::vec3 tmp_bitangent;
    glm::vec3 tmp_tangent;

    const float t = Shapes::Triangles::BufferIntersect(geom, kdTriangles, currentNode.triStartIdx,
                                                       currentNode.triEndIdx, targetRay, tmp_intersect, tmp_normal,
                                                       tmp_bitangent, tmp_tangent, tmp_uv);
    if (t > EPSILON && t < minT)
    {
      minT = t;
      intersectionPoint = tmp_intersect;
      normal = tmp_normal;
      bitangent = tmp_bitangent;
      tangent = tmp_tangent;
      uv = tmp_uv;
    }

    return;
  }

  if (currentNode.leftChildIdx >= 0)
  {
    kdIntersectionTest(currentNode.leftChildIdx, targetRay, transformedRay, geom, kdNodes, kdTriangles,
                       intersectionPoint, normal, bitangent, tangent, uv, minT);
  }

  if (currentNode.rightChildIdx >= 0)
  {
    kdIntersectionTest(currentNode.rightChildIdx, targetRay, transformedRay, geom, kdNodes, kdTriangles,
                       intersectionPoint, normal, bitangent, tangent, uv, minT);
  }
}

__device__ inline void kdIntersectionTest_Queue(const int startIdx, const Ray& targetRay, const Ray& transformedRay,
                                                const Geom& geom, KDNode* kdNodes, Triangle* kdTriangles,
                                                glm::vec3& intersectionPoint, glm::vec3& normal, glm::vec3& bitangent,
                                                glm::vec3& tangent, glm::vec2& uv, float& minT)
{
  KDNode* queueContainer[KD_TREE_QUEUE_SIZE];

  // Starting
  queueContainer[0] = &kdNodes[startIdx];
  int writeIndex = 0;
  int readIndex = 0;

  while (readIndex <= writeIndex && readIndex < KD_TREE_QUEUE_SIZE)
  {
    const KDNode* currentNode = queueContainer[readIndex];
    // Read Current Write Index Node
    const float boundT = boundingBoxIntersectionTest(currentNode->min, currentNode->max, transformedRay);

    // Didn't intersect node's AABB
    if (boundT < EPSILON)
    {
      readIndex++;
      continue;
    }

    // Child Node
    if (currentNode->triStartIdx >= 0)
    {
      glm::vec3 tmp_intersect;
      glm::vec3 tmp_normal;
      glm::vec2 tmp_uv;
      glm::vec3 tmp_bitangent;
      glm::vec3 tmp_tangent;

      const float t = Shapes::Triangles::BufferIntersect(geom, kdTriangles, currentNode->triStartIdx,
                                                         currentNode->triEndIdx, targetRay, tmp_intersect, tmp_normal,
                                                         tmp_bitangent, tmp_tangent, tmp_uv);
      if (t > EPSILON && t < minT)
      {
        minT = t;
        intersectionPoint = tmp_intersect;
        normal = tmp_normal;
        bitangent = tmp_bitangent;
        tangent = tmp_tangent;
        uv = tmp_uv;
      }

      readIndex++;
      continue;
    }

    if (currentNode->leftChildIdx >= 0)
    {
      writeIndex++;
      queueContainer[writeIndex] = &kdNodes[currentNode->leftChildIdx];
    }

    if (currentNode->rightChildIdx >= 0)
    {
      writeIndex++;
      queueContainer[writeIndex] = &kdNodes[currentNode->rightChildIdx];
    }

    readIndex++;
  }
}

namespace Intersections
{
  __device__ inline ShadeableIntersection Do(Ray targetRay, Geom* geometries, int geometrySize, Triangle* triangles,
                                             KDNode* kdNodes, Triangle* kdTriangles)
  {
    float t = -1.0f;
    glm::vec3 intersectPoint;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    float tMin = FLT_MAX;
    int hitGeomIndex = -1;

    glm::vec2 tempUV;
    glm::vec3 tempIntersectPoint;
    glm::vec3 tempNormal;
    glm::vec3 tempBitangent;
    glm::vec3 tempTangent;

    // naive parse through global geoms

    for (int i = 0; i < geometrySize; i++)
    {
      Geom& geom = geometries[i];

      if (geom.type == CUBE)
      {
        t = boxIntersectionTest(geom, targetRay, tempIntersectPoint, tempNormal, tempBitangent, tempTangent);
      }
      else if (geom.type == SPHERE)
      {
        t = sphereIntersectionTest(geom, targetRay, tempIntersectPoint, tempNormal, tempBitangent, tempTangent);
      }
      else if (geom.type == SQUAREPLANE)
      {
        t = Shapes::SquarePlane::Intersect(geom, targetRay, tempIntersectPoint, tempNormal, tempBitangent, tempTangent,
                                           tempUV);
      }
      else if (geom.type == MESH)
      {
#ifdef ENABLE_MESH_BOUNDING_CULL
        const Ray r_loc{
          multiplyMV(geom.inverseTransform, glm::vec4(targetRay.origin, 1.0f)),
          glm::normalize(multiplyMV(geom.inverseTransform, glm::vec4(targetRay.direction, 0.0f)))
        };
        const float temp = boundingBoxIntersectionTest(geom.min, geom.max, r_loc);

        if (temp > EPSILON)
        {
#endif
          t = Shapes::Triangles::BulkIntersect(geom, triangles, targetRay, tempIntersectPoint, tempNormal,
                                               tempBitangent, tempTangent, tempUV);

#ifdef ENABLE_MESH_BOUNDING_CULL
        }
#endif
      }
      else if (geom.type == ACCELERATED_MESH)
      {
        const int startIdx = geom.kdRootNodeIndex;

        Ray r_loc;
        r_loc.origin = multiplyMV(geom.inverseTransform, glm::vec4(targetRay.origin, 1.0f));
        r_loc.direction = glm::normalize(multiplyMV(geom.inverseTransform, glm::vec4(targetRay.direction, 0.0f)));

        float meshT = FLT_MAX;

        kdIntersectionTest_Queue(startIdx, targetRay, r_loc, geom, kdNodes, kdTriangles, tempIntersectPoint, tempNormal,
                                 tempBitangent, tempTangent, tempUV, meshT);
        t = meshT;
      }

      // Compute the minimum t from the intersection tests to determine what
      // scene geometry object was hit first.
      if (t > EPSILON && tMin > t)
      {
        tMin = t;
        hitGeomIndex = i;
        intersectPoint = tempIntersectPoint;
        normal = tempNormal;
        bitangent = tempBitangent;
        tangent = tempTangent;
        uv = tempUV;
      }
    }

    ShadeableIntersection result = {};
    if (hitGeomIndex != -1)
    {
      // The ray hits something
      result.t = tMin;
      result.geom = &geometries[hitGeomIndex];
      result.materialId = geometries[hitGeomIndex].materialid;
      result.intersectPoint = intersectPoint;
      result.uv = uv;
      result.surfaceNormal = normal;
      result.tangentToWorld = glm::mat3(
        tangent,
        bitangent,
        normal
      );

      result.worldToTangent = glm::transpose(result.tangentToWorld);
    }

    return result;
  }

  __host__ __device__ inline Ray SpawnRay(const Point3f& origin, const Normal3f& normal, const Vector3f& d)
  {
    Vector3f originOffset = normal * 0.0005f;
    // Make sure to flip the direction of the offset so it's in
    // the same general direction as the ray direction
    originOffset = (glm::dot(d, normal) > 0) ? originOffset : -originOffset;
    const Point3f o(origin + originOffset);
    return Ray{o, d};
  }
}

// The MIT License (MIT)
// Copyright (c) 2014 Matthew Klingensmith and Ivan Dryanovski
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <open_chisel/ProjectionIntegrator.h>
#include <open_chisel/geometry/Raycast.h>

namespace chisel
{

  ProjectionIntegrator::ProjectionIntegrator()
  {
    // TODO Auto-generated constructor stub

  }

  ProjectionIntegrator::ProjectionIntegrator(const TruncatorPtr& t, const WeighterPtr& w, float crvDist, bool enableCrv, const Vec3List& centers) :
    truncator(t), weighter(w), carvingDist(crvDist), enableVoxelCarving(enableCrv), centroids(centers)
  {

  }

  bool ProjectionIntegrator::Integrate(const PointCloud& cloud, const Transform& cameraPose, Chunk* chunk) const
  {
    assert(!!chunk);

    if(cloud.HasColor() && chunk->HasColors())
      {
        return IntegrateColorPointCloud(cloud, cameraPose, chunk);
      }
    else
      {
        return IntegratePointCloud(cloud, cameraPose, chunk);
      }
  }


  bool ProjectionIntegrator::IntegratePointCloud(const PointCloud& cloud, const Transform& cameraPose, Chunk* chunk) const
  {
    const float roundingFactor = 1.0f / chunk->GetVoxelResolutionMeters();

    Point3List raycastVoxels;
    Point3 chunkMin = Point3::Zero();
    Point3 chunkMax = chunk->GetNumVoxels();
    bool updated = false;
    Vec3 startCamera = cameraPose.translation();
    for (const Vec3& point : cloud.GetPoints())
      {
        Vec3 worldPoint = cameraPose * point;
        const Vec3 distance = worldPoint - startCamera;
        float depth = distance.norm();
        Vec3 dir = distance.normalized();
        float truncation = truncator->GetTruncationDistance(depth);
        Vec3 start = worldPoint - dir * truncation - chunk->GetOrigin();
        Vec3 end = worldPoint + dir * truncation - chunk->GetOrigin();
        start.x() *= roundingFactor;
        start.y() *= roundingFactor;
        start.z() *= roundingFactor;
        end.x() *= roundingFactor;
        end.y() *= roundingFactor;
        end.z() *= roundingFactor;
        raycastVoxels.clear();
        Raycast(start, end, chunkMin, chunkMax, &raycastVoxels);

        for (const Point3& voxelCoords : raycastVoxels)
          {
            VoxelID id = chunk->GetVoxelID(voxelCoords);
            DistVoxel& distVoxel = chunk->GetDistVoxelMutable(id);
            const Vec3& centroid = centroids[id] + chunk->GetOrigin();
            float u = depth - (centroid - startCamera).norm();
            float weight = weighter->GetWeight(u, truncation);
            if (fabs(u) < truncation)
              {
                distVoxel.Integrate(u, weight);
                updated = true;
              }
            else if (enableVoxelCarving && u > truncation + carvingDist)
              {
                if (distVoxel.GetWeight() > 0)
                  {
                    distVoxel.Integrate(1.0e-5, 5.0f);
                    updated = true;
                  }
              }
          }
      }
    return updated;
  }

  bool ProjectionIntegrator::IntegrateColorPointCloud(const PointCloud& cloud, const Transform& cameraPose, Chunk* chunk) const
  {
    const float roundingFactor = 1.0f / chunk->GetVoxelResolutionMeters();

    Point3List raycastVoxels;
    Point3 chunkMin = Point3::Zero();
    Point3 chunkMax = chunk->GetNumVoxels();
    bool updated = false;
    size_t i = 0;
    Vec3 startCamera = cameraPose.translation();
    Transform inversePose = cameraPose.inverse();
    for (const Vec3& point : cloud.GetPoints())
      {
        const Vec3& color = cloud.GetColors()[i];
        Vec3 worldPoint = cameraPose * point;
        const Vec3 distance = worldPoint - startCamera;
        float depth = distance.norm();
        Vec3 dir = distance.normalized();
        float truncation = truncator->GetTruncationDistance(depth);
        Vec3 start = worldPoint - dir * truncation - chunk->GetOrigin();
        Vec3 end = worldPoint + dir * truncation - chunk->GetOrigin();
        start.x() *= roundingFactor;
        start.y() *= roundingFactor;
        start.z() *= roundingFactor;
        end.x() *= roundingFactor;
        end.y() *= roundingFactor;
        end.z() *= roundingFactor;
        raycastVoxels.clear();
        Raycast(start, end, chunkMin, chunkMax, &raycastVoxels);

        for (const Point3& voxelCoords : raycastVoxels)
          {
            VoxelID id = chunk->GetVoxelID(voxelCoords);
            ColorVoxel& voxel = chunk->GetColorVoxelMutable(id);
            DistVoxel& distVoxel = chunk->GetDistVoxelMutable(id);
            const Vec3& centroid = centroids[id] + chunk->GetOrigin();
            float u = depth - (inversePose * centroid - startCamera).norm();
            float weight = weighter->GetWeight(u, truncation);
            if (fabs(u) < truncation)
              {
                distVoxel.Integrate(u, weight);
                voxel.Integrate((uint8_t)(color.x() * 255.0f), (uint8_t)(color.y() * 255.0f), (uint8_t)(color.z() * 255.0f), 2);
                updated = true;
              }
            else if (enableVoxelCarving && u > truncation + carvingDist)
              {
                if (distVoxel.GetWeight() > 0)
                  {
                    distVoxel.Integrate(1.0e-5, 5.0f);
                    updated = true;
                  }
              }
          }
        i++;

      }
    return updated;
  }

  bool ProjectionIntegrator::IntegrateChunk(const Chunk* chunkToIntegrate, Chunk* chunk) const{

    assert(chunk != nullptr && chunkToIntegrate != nullptr);

    bool updated = false;

    for (size_t i = 0; i < centroids.size(); i++)
    {
        DistVoxel& voxel = chunk->GetDistVoxelMutable(i);
        DistVoxel voxelToIntegrate = chunkToIntegrate->GetDistVoxel(i);

        if (voxelToIntegrate.GetWeight() > 0 && voxelToIntegrate.GetSDF() <99999)
        {
          voxel.Integrate(voxelToIntegrate.GetSDF(), voxelToIntegrate.GetWeight());
          updated = true;
        }

        if(enableVoxelCarving)
        {
          if (voxel.GetWeight() > 0 && voxel.GetSDF() < 1e-5)
          {
            voxel.Carve();
            updated = true;
          }
        }
    }
    return updated;
  }

  bool ProjectionIntegrator::IntegrateColorChunk(const Chunk* chunkToIntegrate, Chunk* chunk) const{

    assert(chunk != nullptr && chunkToIntegrate != nullptr);

    bool updated = false;

    for (size_t i = 0; i < centroids.size(); i++)
    {
        DistVoxel& distVoxel = chunk->GetDistVoxelMutable(i);
        ColorVoxel& colorVoxel = chunk->GetColorVoxelMutable(i);

        const DistVoxel& distVoxelToIntegrate = chunkToIntegrate->GetDistVoxel(i);
        const ColorVoxel& colorVoxelToIntegrate = chunkToIntegrate->GetColorVoxel(i);

        if (distVoxelToIntegrate.GetWeight() > 0 && distVoxelToIntegrate.GetSDF() <99999)
        {
          distVoxel.Integrate(distVoxelToIntegrate.GetSDF(), distVoxelToIntegrate.GetWeight());
          colorVoxel.Integrate((uint8_t) colorVoxelToIntegrate.GetRed(), (uint8_t) colorVoxelToIntegrate.GetGreen(), (uint8_t)  colorVoxelToIntegrate.GetBlue(), colorVoxelToIntegrate.GetWeight());

          updated = true;
        }

        if(enableVoxelCarving)
        {
          if (distVoxel.GetWeight() > 0 && distVoxel.GetSDF() < 1e-5)
          {
            distVoxel.Carve();
            colorVoxel.Reset();
            updated = true;
          }
        }
    }
    return updated;
  }


  ProjectionIntegrator::~ProjectionIntegrator()
  {
    // TODO Auto-generated destructor stub
  }

} // namespace chisel 

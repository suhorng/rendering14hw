
/*
  pbrt source code Copyright(c) 1998-2012 Matt Pharr and Greg Humphreys.

  This file is part of pbrt.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

  - Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  - Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */

#define PHONG_INTERPOLATION 0

// shapes/heightfield2.cpp*
#include "stdafx.h"
#include "shapes/heightfield2.h"
#include "core/geometry.h"
#include "core/transform.h"
#include "paramset.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <limits>
#include <vector>

using std::vector;
using std::array;
using std::pair;

struct Heightfield2_impl {
  const int nx, ny;
  const float dx, dy;
  float * const z, minz, maxz;
  BBox bbox;
  vector<Point> pt;
#if PHONG_INTERPOLATION
  vector<vector<Vector>> n;
  vector<vector<vector<pair<int,int>>>> tri;
  vector<vector<DifferentialGeometry>> tri_dg;
#endif
};

#define COORD(y,x) ((y)*impl->nx + (x))

// Heightfield2 Method Definitions
Heightfield2::Heightfield2(const Transform *o2w, const Transform *w2o,
    bool ro, int x, int y, const float *zs)
  : Shape(o2w, w2o, ro),
    impl(new Heightfield2_impl {x, y, 1.0f/(x - 1), 1.0f/(y - 1),
                                new float[x*y]})
{
  memcpy(impl->z, zs, impl->nx*impl->ny*sizeof(float));
  ///////////////////////////////////////////////////////////////////
  for (int y = 0; y < impl->ny; ++y)
    for (int x = 0; x < impl->nx; ++x)
      impl->pt.emplace_back( x*impl->dx, y*impl->dy, impl->z[COORD(y,x)] );
  ///////////////////////////////////////////////////////////////////
  float minz = impl->z[0], maxz = impl->z[0];
  for (int i = 1; i < impl->nx*impl->ny; ++i) {
    if (impl->z[i] < minz) minz = impl->z[i];
    if (impl->z[i] > maxz) maxz = impl->z[i];
  }
  impl->minz = minz;
  impl->maxz = maxz;
  impl->bbox = BBox(Point(0,0,impl->minz), Point(1,1,impl->maxz));
  ///////////////////////////////////////////////////////////////////
#if PHONG_INTERPOLATION
  for (int y = 0; y < impl->ny-1; ++y) {
    impl->tri.push_back({});
    for (int x = 0; x < impl->nx-1; ++x) {
      impl->tri.back().push_back({{y,x}, {y,x+1},   {y+1,x+1}});
      impl->tri.back().push_back({{y,x}, {y+1,x+1}, {y+1,x}});
    }
  }
  ///////////////////////////////////////////////////////////////////
  const Transform &real_o2w = *o2w;
  const Normal dndu {0,0,0}, dndv {0,0,0};
  for (int y = 0; y < impl->ny-1; ++y) {
    impl->tri_dg.push_back({});
    for (int x = 0; x < impl->nx-1; ++x) {
      for (int k = 0; k < 2; ++k) {
        const vector<pair<int,int>> &_pt = impl->tri[y][x*2+k];
        const Point &pt0 = impl->pt[COORD(_pt[0].first, _pt[0].second)]
                  , &pt1 = impl->pt[COORD(_pt[1].first, _pt[1].second)]
                  , &pt2 = impl->pt[COORD(_pt[2].first, _pt[2].second)];
        const Vector ntri {Cross(pt1-pt0, pt2-pt0)};
        const float z_inv = 1.0f/ntri.z;
        const Vector dpdu {1,0,-ntri.x*z_inv}, dpdv {0,1,-ntri.y*z_inv};
        impl->tri_dg.back().emplace_back(Point(), real_o2w(dpdu), real_o2w(dpdv),
                                                  real_o2w(dndu), real_o2w(dndv),
                                                  -1.f, -1.f, this);
        impl->tri_dg.back().back().dudx = 1;
        impl->tri_dg.back().back().dvdy = 1;
      }
    }
  }
  ///////////////////////////////////////////////////////////////////
  vector<vector<int>> cnt;
  cnt.resize(impl->ny);
  for (int y = 0; y < impl->ny; ++y)
    cnt[y].resize(impl->nx);
  impl->n.resize(impl->ny);
  for (int y = 0; y < impl->ny; ++y)
    impl->n[y].resize(impl->nx);
  for (int y = 0; y < impl->ny-1; ++y) {
    for (int kx = 0; kx < 2*(impl->nx-1); ++kx) {
      for (int i = 0; i < 3; ++i) {
        const int &pty = impl->tri[y][kx][i].first, &ptx = impl->tri[y][kx][i].second;
        ++cnt[pty][ptx];
        impl->n[pty][ptx] += Vector {impl->tri_dg[y][kx].nn};
      }
    }
  }
  for (int y = 0; y < impl->ny; ++y)
    for (int x = 0; x < impl->nx; ++x)
      impl->n[y][x] /= cnt[y][x];
#endif
}


Heightfield2::~Heightfield2() {
  delete[] impl->z;
  delete impl;
}


BBox Heightfield2::ObjectBound() const {
  return impl->bbox;
}


void Heightfield2::Refine(vector<Reference<Shape>> &) const { throw std::runtime_error("Heightfield2::Refine"); }

static const float EPS = 1e-10;

inline static bool jiao_tri(
  const Heightfield2_impl *impl,
  const Ray &ray,
#if PHONG_INTERPOLATION
  const vector<pair<int,int>>& _pt,
#else
  const int& y0, const int& x0,
  const int& y1, const int& x1,
  const int& y2, const int& x2,
#endif
  float* tHit)
{
#if PHONG_INTERPOLATION
  const Point &pt0 = impl->pt[COORD(_pt[0].first, _pt[0].second)]
            , &pt1 = impl->pt[COORD(_pt[1].first, _pt[1].second)]
            , &pt2 = impl->pt[COORD(_pt[2].first, _pt[2].second)];
#else
  const Point &pt0 = impl->pt[COORD(y0, x0)]
            , &pt1 = impl->pt[COORD(y1, x1)]
            , &pt2 = impl->pt[COORD(y2, x2)];
#endif
  const Vector e1 = pt1 - pt0;
  const Vector e2 = pt2 - pt0;
  const Vector s1 = Cross(ray.d, e2);
  const float divisor = Dot(s1, e1);

  if (divisor == 0.)
      return false;
  const float invDivisor = 1.f / divisor;

  // Compute first barycentric coordinate
  const Vector s = ray.o - pt0;
  float b1 = Dot(s, s1) * invDivisor;
  if (b1 < 0. || b1 > 1.)
      return false;

  // Compute second barycentric coordinate
  Vector s2 = Cross(s, e1);
  float b2 = Dot(ray.d, s2) * invDivisor;
  if (b2 < 0. || b1 + b2 > 1.)
      return false;

  // Compute _t_ to intersection point
  float t = Dot(e2, s2) * invDivisor;
  if (t < ray.mint || t > ray.maxt)
    return false;

  *tHit = t;
  return true;
}

bool Heightfield2::Intersect(
  const Ray &r,
  float *tHit,
  float *rayEpsilon,
  DifferentialGeometry *dg) const
{
  Ray ray;
  (*WorldToObject)(r, &ray);

  float t_init;
  const float d_inv[3] { 1.0f/ray.d.x, 1.0f/ray.d.y, 1.0f/ray.d.z };
  if (impl->bbox.Inside(ray(ray.mint))) {
    t_init = ray.mint;
  } else if (!impl->bbox.IntersectP(ray, &t_init)) {
    return false;
  }

  int coord[3] = { static_cast<int>((ray.o.x + t_init*ray.d.x)*(impl->nx-1) + EPS)
                 , static_cast<int>((ray.o.y + t_init*ray.d.y)*(impl->ny-1) + EPS)
                 , 0 };

  if (coord[0] == impl->nx-1) --coord[0];
  if (coord[1] == impl->ny-1) --coord[1];

  float t_nxt[3], t_step[3];
  int coord_step[3], boundary[3];

  // handle z
  if (ray.d[2] >= 0) {
    t_nxt[2] = (impl->maxz - ray.o.z)*d_inv[2];
    t_step[2] = (impl->maxz-impl->minz)*d_inv[2]; // unimportant
    coord_step[2] = 1;
    boundary[2] = 1;
  } else {
    t_nxt[2] = (impl->minz - ray.o.z)*d_inv[2];
    t_step[2] = -(impl->maxz-impl->minz)*d_inv[2]; // unimportant
    coord_step[2] = -1;
    boundary[2] = -1;
  }

  // handle y
  if (ray.d[1] >= 0) {
    t_nxt[1] = ((coord[1]+1)*impl->dy - ray.o.y)*d_inv[1];
    t_step[1] = impl->dy * d_inv[1];
    coord_step[1] = 1;
    boundary[1] = impl->ny - 1;
  } else {
    t_nxt[1] = (coord[1]*impl->dy - ray.o.y)*d_inv[1];
    t_step[1] = - impl->dy * d_inv[1];
    coord_step[1] = -1;
    boundary[1] = -1;
  }

  // handle x
  if (ray.d[0] >= 0) {
    t_nxt[0] = ((coord[0]+1)*impl->dx - ray.o.x)*d_inv[0];
    t_step[0] = impl->dx * d_inv[0];
    coord_step[0] = 1;
    boundary[0] = impl->nx - 1;
  } else {
    t_nxt[0] = (coord[0]*impl->dx - ray.o.x)*d_inv[0];
    t_step[0] = - impl->dx * d_inv[0];
    coord_step[0] = -1;
    boundary[0] = -1;
  }

  bool hit = false;
  int argtri[3] {0,0,0};
  float tmin = std::numeric_limits<float>::max();

  for (;;) {
#if PHONG_INTERPOLATION
    for (int k = 0; k < 2; ++k) {
      float t;
      if (jiao_tri(impl, ray, impl->tri[coord[1]][coord[0]*2+k], &t)) {
        hit = true;
        if (t < tmin) {
          tmin = t;
          argtri[0] = coord[1];
          argtri[1] = coord[0]*2+k;
        }
      }
    }
#else
    float t;
    const int &y = coord[1], &x = coord[0];
    if (jiao_tri(impl, ray, y,   x,
                            y,   x+1,
                            y+1, x+1, &t)) {
      hit = true;
      if (t < tmin) {
        tmin = t;
        argtri[0] = COORD(y,x);
        argtri[1] = COORD(y,x+1);
        argtri[2] = COORD(y+1,x+1);
      }
    }
    if (jiao_tri(impl, ray, y,   x,
                            y+1, x+1,
                            y+1, x, &t)) {
      hit = true;
      if (t < tmin) {
        tmin = t;
        argtri[0] = COORD(y,x);
        argtri[1] = COORD(y+1,x+1);
        argtri[2] = COORD(y+1,x);
      }
    }
#endif
    if (hit) break;
    int cmp_res = ((t_nxt[0]>=t_nxt[1])<<2)
                | ((t_nxt[0]>=t_nxt[2])<<1)
                | (t_nxt[1]>=t_nxt[2]);
    static const int lookup_cmp[8] = { 0           // 0<1   0<2   1<2
                                     , 0           // 0<1   0<2   2<=1
                                     , 2147483647  // 0<1   2<=0  1<2
                                     , 2           // 0<1   2<=0  2<=1
                                     , 1           // 1<=0  0<2   1<2
                                     , 2147483647  // 1<=0  0<2   2<=1
                                     , 1           // 1<=0  2<=0  1<2
                                     , 2 };        // 1<=0  2<=0  2<=1
    int k = lookup_cmp[cmp_res];
    if (t_nxt[k] > ray.maxt) break;
    coord[k] += coord_step[k];
    if (coord[k] == boundary[k]) break;
    t_nxt[k] += t_step[k];
  }

  if (!hit) return false;

  const Point pt {ray(tmin)};
#if PHONG_INTERPOLATION
  *dg = impl->tri_dg[argtri[0]][argtri[1]];
  dg->p = (*ObjectToWorld)(pt);
  dg->u = pt.x;
  dg->v = pt.y;
#else
  const Vector ntri {Cross( impl->pt[argtri[1]]-impl->pt[argtri[0]]
                          , impl->pt[argtri[2]]-impl->pt[argtri[0]] )};
  const float z_inv = 1.0f/ntri.z;
  const Vector dpdu {1,0,-ntri.x*z_inv}, dpdv {0,1,-ntri.y*z_inv};
  static const Normal dndu {0,0,0}, dndv {0,0,0};
  const Transform &o2w = *ObjectToWorld;
  *dg = { o2w(pt), o2w(dpdu), o2w(dpdv), o2w(dndu), o2w(dndv), pt.x, pt.y, this };
  dg->dudx = 1;
  dg->dvdy = 1;
#endif
  *tHit = tmin;
  *rayEpsilon = 1e-3f * tmin;
  return true;
}


bool Heightfield2::IntersectP(const Ray &r) const {
  Ray ray;
  (*WorldToObject)(r, &ray);

  float t_init;
  const float d_inv[3] { 1.0f/ray.d.x, 1.0f/ray.d.y, 1.0f/ray.d.z };
  if (impl->bbox.Inside(ray(ray.mint))) {
    t_init = ray.mint;
  } else if (!impl->bbox.IntersectP(ray, &t_init)) {
    return false;
  }

  int coord[3] = { static_cast<int>((ray.o.x + t_init*ray.d.x)*(impl->nx-1) + EPS)
                 , static_cast<int>((ray.o.y + t_init*ray.d.y)*(impl->ny-1) + EPS)
                 , 0 };

  if (coord[0] == impl->nx-1) --coord[0];
  if (coord[1] == impl->ny-1) --coord[1];

  float t_nxt[3], t_step[3];
  int coord_step[3], boundary[3];

  // handle z
  if (ray.d[2] >= 0) {
    t_nxt[2] = (impl->maxz - ray.o.z)*d_inv[2];
    t_step[2] = (impl->maxz-impl->minz)*d_inv[2]; // unimportant
    coord_step[2] = 1;
    boundary[2] = 1;
  } else {
    t_nxt[2] = (impl->minz - ray.o.z)*d_inv[2];
    t_step[2] = -(impl->maxz-impl->minz)*d_inv[2]; // unimportant
    coord_step[2] = -1;
    boundary[2] = -1;
  }

  // handle y
  if (ray.d[1] >= 0) {
    t_nxt[1] = ((coord[1]+1)*impl->dy - ray.o.y)*d_inv[1];
    t_step[1] = impl->dy * d_inv[1];
    coord_step[1] = 1;
    boundary[1] = impl->ny - 1;
  } else {
    t_nxt[1] = (coord[1]*impl->dy - ray.o.y)*d_inv[1];
    t_step[1] = - impl->dy * d_inv[1];
    coord_step[1] = -1;
    boundary[1] = -1;
  }

  // handle x
  if (ray.d[0] >= 0) {
    t_nxt[0] = ((coord[0]+1)*impl->dx - ray.o.x)*d_inv[0];
    t_step[0] = impl->dx * d_inv[0];
    coord_step[0] = 1;
    boundary[0] = impl->nx - 1;
  } else {
    t_nxt[0] = (coord[0]*impl->dx - ray.o.x)*d_inv[0];
    t_step[0] = - impl->dx * d_inv[0];
    coord_step[0] = -1;
    boundary[0] = -1;
  }

  for (;;) {
#if PHONG_INTERPOLATION
    for (int k = 0; k < 2; ++k) {
      float t;
      if (jiao_tri(impl, ray, impl->tri[coord[1]][coord[0]*2+k], &t))
        return true;
    }
#else
    float t;
    const int &y = coord[1], &x = coord[0];
    if (jiao_tri(impl, ray, y,   x,
                            y,   x+1,
                            y+1, x+1, &t)
     || jiao_tri(impl, ray, y,   x,
                            y+1, x+1,
                            y+1, x, &t))
    {
      return true;
    }
#endif
    int cmp_res = ((t_nxt[0]>=t_nxt[1])<<2)
                | ((t_nxt[0]>=t_nxt[2])<<1)
                | (t_nxt[1]>=t_nxt[2]);
    static const int lookup_cmp[8] = { 0           // 0<1   0<2   1<2
                                     , 0           // 0<1   0<2   2<=1
                                     , 2147483647  // 0<1   2<=0  1<2
                                     , 2           // 0<1   2<=0  2<=1
                                     , 1           // 1<=0  0<2   1<2
                                     , 2147483647  // 1<=0  0<2   2<=1
                                     , 1           // 1<=0  2<=0  1<2
                                     , 2 };        // 1<=0  2<=0  2<=1
    int k = lookup_cmp[cmp_res];
    if (t_nxt[k] > ray.maxt) break;
    coord[k] += coord_step[k];
    if (coord[k] == boundary[k]) break;
    t_nxt[k] += t_step[k];
  }
  return false;
}

inline static float det3( const float& f0, const float& f3, const float& f6
                        , const float& f1, const float& f4, const float& f7
                        , const float& f2, const float& f5, const float& f8 )
{
  return f0*(f4*f8-f5*f7)-f3*(f1*f8-f2*f7)+f6*(f1*f5-f2*f4);
}

void Heightfield2::GetShadingGeometry(
  const Transform &o2w,
  const DifferentialGeometry &dg,
  DifferentialGeometry *dgShading) const
{
#if PHONG_INTERPOLATION
  const int ty = static_cast<int>(dg.v*(impl->ny-1) + EPS);
  const int x = static_cast<int>(dg.u*(impl->nx-1) + EPS);
  const int tx = static_cast<int>(x*2 + (dg.u - x*impl->dx <= dg.v - ty*impl->dy));
  const vector<pair<int,int>> &_pt = impl->tri[ty][tx];
  const Point &p0 = o2w(impl->pt[COORD(_pt[0].first, _pt[0].second)])
            , &p1 = o2w(impl->pt[COORD(_pt[1].first, _pt[1].second)])
            , &p2 = o2w(impl->pt[COORD(_pt[2].first, _pt[2].second)])
            , p = dg.p;
  const float w[3] { det3( p.x,  p1.x, p2.x
                         , p.y,  p1.y, p2.y
                         , p.z,  p1.z, p2.z )
                   , det3( p0.x, p.x,  p2.x
                         , p0.y, p.y,  p2.y
                         , p0.z, p.z,  p2.z )
                   , det3( p0.x, p1.x, p.x
                         , p0.y, p1.y, p.y
                         , p0.z, p1.z, p.z ) };
  const Vector n { w[0]*impl->n[_pt[0].first][_pt[0].second]
                 + w[1]*impl->n[_pt[1].first][_pt[1].second]
                 + w[2]*impl->n[_pt[2].first][_pt[2].second] };
  std::memcpy(dgShading, &dg, sizeof(DifferentialGeometry));
  dgShading->nn = Normal{Normalize(n)};
#if 0
// not really needed -- only normal is used
// the u,v aren't correct anyway
  const Vector u = Cross(n, Vector {0,0,1});
  const Vector v = Cross(u, n);
  dgShading->dpdu = u;
  dgShading->dpdv = v;
#endif
#else
  *dgShading = dg;
#endif
}


Heightfield2 *CreateHeightfieldShape2(
  const Transform *o2w,
  const Transform *w2o,
  bool reverseOrientation,
  const ParamSet &params)
{
  int nu = params.FindOneInt("nu", -1);
  int nv = params.FindOneInt("nv", -1);
  int nitems;
  const float *Pz = params.FindFloat("Pz", &nitems);
  Assert(nitems == nu*nv);
  Assert(nu != -1 && nv != -1 && Pz != NULL);
  return new Heightfield2(o2w, w2o, reverseOrientation, nu, nv, Pz);
}


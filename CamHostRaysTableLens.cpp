#include "CamHostPluginAPI.h"
#include <iostream>
#include <thread> // just for test big delay
#include <chrono> // std::chrono::seconds

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>
#include <cstring>
#include <cassert>
#include <algorithm>

#include "../HydraCore/hydra_drv/cglobals.h"
#include "../HydraAPI/hydra_api/HydraAPI.h"

#include "Bitmap.h"

struct PipeThrough
{
  float cosPower4      = 1.0f;
  uint32_t packedIndex = 0;
};

class TableLens : public IHostRaysAPI
{
public:
  TableLens() { hr_qmc::init(table); m_globalCounter = 0; }
  
  void SetParameters(int a_width, int a_height, const float a_projInvMatrix[16], const wchar_t* a_camNodeText) override
  {
    m_width   = a_width;
    m_height  = a_height;
    m_fwidth  = float(a_width);
    m_fheight = float(a_height);
    m_aspect  = m_fheight / m_fwidth;
    CalcPhysSize();
    memcpy(&m_projInv, a_projInvMatrix, sizeof(float4x4));

    m_doc.load_string(a_camNodeText);
    ReadParamsFromNode(m_doc.child(L"camera"));
    RunTestRays();
  }

  void ReadParamsFromNode(pugi::xml_node a_camNode);
  void RunTestRays();

  void MakeRaysBlock(RayPart1* out_rayPosAndNear, RayPart2* out_rayDirAndFar, size_t in_blockSize, int passId) override;
  void AddSamplesContribution(float* out_color4f, const float* colors4f, size_t in_blockSize, uint32_t a_width, uint32_t a_height, int passId) override;
  void FinishRendering() override;

  pugi::xml_document m_doc;

  unsigned int table[hr_qmc::QRNG_DIMENSIONS][hr_qmc::QRNG_RESOLUTION];
  unsigned int m_globalCounter = 0;

  float m_fwidth  = 1024.0f;
  float m_fheight = 1024.0f;
  int m_width;
  int m_height;
  float m_aspect  = 1.0f;
  float2 m_physSize;
  float4x4 m_projInv;
  float m_diagonal    = 1.0f; // on meter

  mutable std::vector<float3> m_debugPos;
  bool m_enableDebug = false;
  double m_sppDone = 0.0;
  float* m_lastFbPointer = nullptr;
  //////////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////////
  

  bool TraceLensesFromFilm(const float3 inRayPos, const float3 inRayDir, 
                           float3* outRayPos, float3* outRayDir) const;

  bool  IntersectSphericalElement(float radius, float zCenter, const float3 rayPos, const float3 rayDir, 
                                  float *t, float3 *n) const;

  std::vector<PipeThrough> m_pipeline[HOST_RAYS_PIPELINE_LENGTH];

  struct LensElementInterface {
    float curvatureRadius;
    float thickness;
    float eta;
    float apertureRadius;
  };

  struct LensElementInterfaceWithId {
    LensElementInterface lensElement;
    int id;
  };
  
  std::vector<LensElementInterface> lines;

  inline float LensRearZ()      const { return lines[0].thickness; }
  inline float LensRearRadius() const { return lines[0].apertureRadius; }

  
  void CalcPhysSize()
  {
    m_physSize.x = 2.0f*std::sqrt(m_diagonal * m_diagonal / (1.0f + m_aspect * m_aspect));
    m_physSize.y = m_aspect * m_physSize.x;
  }
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string  ws2s(const std::wstring& s);

void TableLens::ReadParamsFromNode(pugi::xml_node a_camNode)
{
  auto opticalSys = a_camNode.child(L"optical_system");
  if(opticalSys == nullptr)
  {
    std::string camName = ws2s(a_camNode.attribute(L"name").as_string());
    std::cout << "[TableLens::ReadParamsFromNode]: node 'optical_system' is not found for camera " << camName.c_str() << std::endl;
    return;
  }
  
  float scale = 1.0f;
  if(opticalSys.attribute(L"scale") != nullptr)
    scale = opticalSys.attribute(L"scale").as_float();

  m_diagonal = opticalSys.attribute(L"sensor_diagonal").as_float();
  CalcPhysSize();

  std::vector<LensElementInterfaceWithId> ids;
  int currId = 0;
  for(auto line : opticalSys.children(L"line"))
  {
    LensElementInterface layer;
    int id = currId;
    if(line.attribute(L"id") != nullptr)
      id = line.attribute(L"id").as_int();
    layer.curvatureRadius = scale*line.attribute(L"curvature_radius").as_float();
    layer.thickness       = scale*line.attribute(L"thickness").as_float();
    layer.eta             = line.attribute(L"ior").as_float();
    if(line.attribute(L"semi_diameter") != nullptr)
      layer.apertureRadius  = scale*2.0f*line.attribute(L"semi_diameter").as_float();
    else if(line.attribute(L"aperture_radius") != nullptr)
      layer.apertureRadius  = scale*1.0f*line.attribute(L"aperture_radius").as_float();
    
    LensElementInterfaceWithId layer2;
    layer2.lensElement = layer;
    layer2.id          = id;
    ids.push_back(layer2);
    currId++;
  }
  
  // you may sort 'lines' by 'ids' if you want 
  //
  std::wstring order = opticalSys.attribute(L"order").as_string();
  if(order == L"scene_to_sensor")
    std::sort(ids.begin(), ids.end(), [](const auto& a, const auto& b) { return a.id > b.id; });
  else
    std::sort(ids.begin(), ids.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

  lines.resize(ids.size());
  for(size_t i=0;i<ids.size(); i++)
    lines[i] = ids[i].lensElement;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static constexpr float MachineEpsilon = std::numeric_limits<float>::epsilon();

static inline bool Quadratic(float A, float B, float C, float *t0, float *t1) {
  // Find quadratic discriminant
  double discrim = (double)B * (double)B - 4. * (double)A * (double)C;
  if (discrim < 0.) 
    return false;
  double rootDiscrim = std::sqrt(discrim);
  float floatRootDiscrim   = rootDiscrim;
  //float floatRootDiscrimErr = MachineEpsilon * rootDiscrim;
  // Compute quadratic _t_ values
  float q;
  if ((float)B < 0)
      q = -.5 * (B - floatRootDiscrim);
  else
      q = -.5 * (B + floatRootDiscrim);
  *t0 = q / A;
  *t1 = C / q;
  if ((float)*t0 > (float)*t1) 
    std::swap(*t0, *t1);
  return true;
}

static inline bool Refract(const float3 wi, const float3 n, float eta, float3 *wt) {
  // Compute $\cos \theta_\roman{t}$ using Snell's law
  float cosThetaI  = dot(n, wi);
  float sin2ThetaI = std::max(float(0), float(1.0f - cosThetaI * cosThetaI));
  float sin2ThetaT = eta * eta * sin2ThetaI;
  // Handle total internal reflection for transmission
  if (sin2ThetaT >= 1) return false;
  float cosThetaT = std::sqrt(1 - sin2ThetaT);
  *wt = eta * -wi + (eta * cosThetaI - cosThetaT) * n;
  return true;
}


static inline float3 faceforward(const float3 n, const float3 v) { return (dot(n, v) < 0.f) ? -n : n; }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool TableLens::IntersectSphericalElement(float radius, float zCenter, const float3 rayPos, const float3 rayDir, 
                                          float *t, float3 *n) const
{
  // Compute _t0_ and _t1_ for ray--element intersection
  const float3 o = rayPos - float3(0, 0, zCenter);
  const float  A = rayDir.x * rayDir.x + rayDir.y * rayDir.y + rayDir.z * rayDir.z;
  const float  B = 2 * (rayDir.x * o.x + rayDir.y * o.y + rayDir.z * o.z);
  const float  C = o.x * o.x + o.y * o.y + o.z * o.z - radius * radius;
  float  t0, t1;
  if (!Quadratic(A, B, C, &t0, &t1)) 
    return false;
  
  // Select intersection $t$ based on ray direction and element curvature
  bool useCloserT = (rayDir.z > 0.0f) ^ (radius < 0.0);
  *t = useCloserT ? std::min(t0, t1) : std::max(t0, t1);
  if (*t < 0.0f) 
    return false;
  
  // Compute surface normal of element at ray intersection point
  *n = normalize(o + (*t)*rayDir);
  *n = faceforward(*n, -1.0f*rayDir);
  return true;
}

bool TableLens::TraceLensesFromFilm(const float3 inRayPos, const float3 inRayDir, 
                                    float3* outRayPos, float3* outRayDir) const
{
  float elementZ = 0;
  // Transform _rCamera_ from camera to lens system space
  // 
  float3 rayPosLens = float3(inRayPos.x, inRayPos.y, -inRayPos.z);
  float3 rayDirLens = float3(inRayDir.x, inRayDir.y, -inRayDir.z);

  for(int i=0; i<lines.size(); i++)
  {
    const LensElementInterface& element = lines[i];                                  
    // Update ray from film accounting for interaction with _element_
    elementZ -= element.thickness;
    
    // Compute intersection of ray with lens element
    float t;
    float3 n;
    bool isStop = (element.curvatureRadius == 0.0f);
    if (isStop) 
    {
      // The refracted ray computed in the previous lens element
      // interface may be pointed towards film plane(+z) in some
      // extreme situations; in such cases, 't' becomes negative.
      if (rayDirLens.z >= 0.0f) 
        return false;
      t = (elementZ - rayPosLens.z) / rayDirLens.z;
    } 
    else 
    {
      const float radius  = element.curvatureRadius;
      const float zCenter = elementZ + element.curvatureRadius;
      if (!IntersectSphericalElement(radius, zCenter, rayPosLens, rayDirLens, &t, &n))
        return false;
    }

    // Test intersection point against element aperture
    const float3 pHit = rayPosLens + t*rayDirLens;
    if(m_enableDebug)
      m_debugPos.push_back(pHit);
    const float r2    = pHit.x * pHit.x + pHit.y * pHit.y;
    if (r2 > element.apertureRadius * element.apertureRadius) 
      return false;
    
    rayPosLens = pHit;
    // Update ray path for from-scene element interface interaction
    if (!isStop) 
    {
      float3 wt;
      float etaI = lines[i+0].eta;                                                      
      float etaT = (i == lines.size()-1) ? 1.0f : lines[i+1].eta;
      if(etaT == 0.0f)
        etaT = 1.0f;                                                          
      if (!Refract(normalize((-1.0f)*rayDirLens), n, etaI / etaT, &wt))
        return false;
      rayDirLens = wt;
    }

  }

  // Transform _rLens_ from lens system space back to camera space
  //
  (*outRayPos) = float3(rayPosLens.x, rayPosLens.y, -rayPosLens.z);
  (*outRayDir) = float3(rayDirLens.x, rayDirLens.y, -rayDirLens.z);
  return true;  
}

void TableLens::RunTestRays()
{ 
  // PBRT:
  //
  ////float3 rayPos(0,0,1);
  ////float3 rayDir(0,0,-1);
  //float3 rayPos(3.5e-5f, 0, 0.999999821f);
  //float3 rayDir(0,0,-1);
  //bool res = TraceLensesFromFilm(rayPos, rayDir, &rayPos, &rayDir);
  //int a = 2;

  float3 ray_pos       = float3(1.0f, 0.0f, 0);
  const float2 rareSam = float2(0, 0); // 0.125f*m_physSize.x // 7,773602061 // 0.003965f

  const float3 shootTo = float3(rareSam.x, rareSam.y, LensRearZ());
  float3 ray_dir       = normalize(shootTo - ray_pos);
  bool rayIsDead       = false;
  if (!TraceLensesFromFilm(ray_pos, ray_dir, &ray_pos, &ray_dir)) 
  {
    ray_pos = float3(0,-10000000.0,0.0); // shoot ray under the floor
    ray_dir = float3(0,-1,0);
    rayIsDead = true;
  }
  else
  {
    ray_dir = float3(-1,-1,-1)*normalize(ray_dir);
    ray_pos = float3(-1,-1,-1)*ray_pos;
  }

  // Zemax data for thorlabs
  //
  //float3 ray_pos = float3(0.0f, 0.0f, 0);
  //const float2 rareSam = float2(LensRearRadius()*1.0f,0);
  /*
  std::ofstream fout("z_points.csv");
  fout << "X_START; X_HIT; Z_HIT;" << std::endl;
  m_enableDebug = true;
  for(float x = 0.0f; x < LensRearRadius() ; x += 0.0001f) 
  {
    m_debugPos.clear();

    float3 ray_pos = float3(0.0f, 0.0f, 0);
    const float2 rareSam = float2(x, 0); // 0.125f*m_physSize.x // 7,773602061 // 0.003965f
  
    const float3 shootTo = float3(rareSam.x, rareSam.y, LensRearZ());
    float3 ray_dir       = normalize(shootTo - ray_pos);
    bool rayIsDead       = false;
    if (!TraceLensesFromFilm(ray_pos, ray_dir, &ray_pos, &ray_dir)) 
    {
      ray_pos = float3(0,-10000000.0,0.0); // shoot ray under the floor
      ray_dir = float3(0,-1,0);
      rayIsDead = true;
    }
    else
    {
      ray_dir = float3(-1,-1,-1)*normalize(ray_dir);
      ray_pos = float3(-1,-1,-1)*ray_pos;
    }

    if(m_debugPos.size() > 0)
    {
      float3 center(0, 0, -LensRearZ() +  lines[0].curvatureRadius);
      float distToCenter = length(m_debugPos[0] - center);
      if(std::abs(distToCenter - std::abs(lines[0].curvatureRadius) > 1e-5f))
      {
        int a = 2;
        std::cout << std::fixed << std::setw(5) << x << " BAD INTERSECTION POINT" << std::endl;
      }
      fout << std::fixed << std::setw(5) << x << "; " << m_debugPos[0].x << "; " << m_debugPos[0].z << std::endl;
    }
    else
      fout << std::fixed << std::setw(5) << x << "; " << "missed" << std::endl;

  }
  
  fout.close();
  m_enableDebug = false;
  */
}


void TableLens::MakeRaysBlock(RayPart1* out_rayPosAndNear, RayPart2* out_rayDirAndFar, size_t in_blockSize, int passId)
{
  if(m_pipeline[0].size() == 0)
  {
    for(int i=0;i<HOST_RAYS_PIPELINE_LENGTH;i++)
    m_pipeline[i].resize(in_blockSize);
  }

  const int putID = passId % HOST_RAYS_PIPELINE_LENGTH;

  #pragma omp parallel for
  for(int i=0;i<in_blockSize;i++)
  {
    const float sensX = hr_qmc::rndFloat(m_globalCounter+i, 0, table[0]);
    const float sensY = hr_qmc::rndFloat(m_globalCounter+i, 1, table[0]);
    const float lensX = hr_qmc::rndFloat(m_globalCounter+i, 2, table[0]);
    const float lensY = hr_qmc::rndFloat(m_globalCounter+i, 3, table[0]);
    const float2 xy   = 0.25f*m_physSize*float2(2.0f*sensX - 1.0f, 2.0f*sensY - 1.0f);

    const float x  = m_fwidth*sensX;  
    const float y  = m_fheight*sensY;

    float3 ray_pos = float3(xy.x, xy.y, 0);
    
    const float2 rareSam  = LensRearRadius()*2.0f*MapSamplesToDisc(float2(lensX - 0.5f, lensY - 0.5f));
    const float3 shootTo  = float3(rareSam.x, rareSam.y, LensRearZ());
    const float3 ray_dirF = normalize(shootTo - ray_pos);
    const float cosTheta  = std::abs(ray_dirF.z);
    
    float3 ray_dir = ray_dirF;
    bool rayIsDead = false;
    if (!TraceLensesFromFilm(ray_pos, ray_dir, &ray_pos, &ray_dir)) 
    {
      ray_pos = float3(0,-10000000.0,0.0); // shoot ray under the floor
      ray_dir = float3(0,-1,0);
      rayIsDead = true;
    }
    else
    {
      ray_dir = float3(-1,-1,-1)*normalize(ray_dir);
      ray_pos = float3(-1,-1,-1)*ray_pos;
    }

    RayPart1 p1;
    p1.origin[0]   = ray_pos.x;
    p1.origin[1]   = ray_pos.y;
    p1.origin[2]   = ray_pos.z;
    if(!rayIsDead)
      p1.xyPosPacked = packXY1616(int(x), int(y)); 
    else
      p1.xyPosPacked = 0xFFFFFFFF;  // packing this value discard contibution from this ray

    RayPart2 p2;
    p2.direction[0] = ray_dir.x;
    p2.direction[1] = ray_dir.y;
    p2.direction[2] = ray_dir.z;
    p2.dummy        = 0.0f;
    
    PipeThrough pipeData;
    pipeData.cosPower4   = (cosTheta*cosTheta)*(cosTheta*cosTheta);
    pipeData.packedIndex = p1.xyPosPacked;

    out_rayPosAndNear[i] = p1;
    out_rayDirAndFar [i] = p2;
    m_pipeline[putID][i] = pipeData;
  }

  //std::this_thread::sleep_for(std::chrono::milliseconds(50)); // test big delay

  m_globalCounter += unsigned(in_blockSize);
} 

void TableLens::AddSamplesContribution(float* out_color4f, const float* colors4f, size_t in_blockSize, uint32_t a_width, uint32_t a_height, int passId)
{
  const int takeID = (passId + HOST_RAYS_PIPELINE_LENGTH - 2) % HOST_RAYS_PIPELINE_LENGTH;

  float4*       out_color = (float4*)out_color4f;
  const float4* colors    = (const float4*)colors4f;
  
  for (int i = 0; i < in_blockSize; i++)
  {
    const auto color = colors[i];
    const int packedIndex = as_int(color.w);
    const int x      = (packedIndex & 0x0000FFFF);         ///<! extract x position from color.w
    const int y      = (packedIndex & 0xFFFF0000) >> 16;   ///<! extract y position from color.w
    const int offset = y*a_width + x;

    if (x >= 0 && y >= 0 && x < a_width && y < a_height && dot3f(color, color) > 0.0f)
    {
      const PipeThrough& passData = m_pipeline[takeID][i];
      assert(passData.packedIndex == packedIndex);        ///<! check that we actually took data from 'm_pipeline' for right ray
      out_color[offset].x += color.x*passData.cosPower4;
      out_color[offset].y += color.y*passData.cosPower4;
      out_color[offset].z += color.z*passData.cosPower4;
    }
  }
  
  // New after FinishRendering()
  //
  const double contribSPP = double(in_blockSize) / (double(m_fwidth)*double(m_fheight));
  m_sppDone += contribSPP;
  m_lastFbPointer = out_color4f; // jst remember the pointer for demo purposes
}

void TableLens::FinishRendering()
{
  const float normConst = float(1.0/m_sppDone);
  const float invGamma  = 1.0f/2.2f;

  std::vector<uint32_t> pixelData(m_width*m_height);
  const float4* realColor = (const float4*)m_lastFbPointer;

  #pragma omp parallel for
  for(int i=0;i<m_width*m_height;i++)
  {
    float4 color = realColor[i]*normConst;
    color.x      = std::pow(color.x, invGamma);
    color.y      = std::pow(color.y, invGamma);
    color.z      = std::pow(color.z, invGamma);
    color.w      = 1.0f;
    pixelData[i] = RealColorToUint32(clamp(color, 0.0f, 1.0f));
  }

  SaveBMP("z_alex_image.bmp", pixelData.data(), m_width, m_height);
}

IHostRaysAPI* CreateTableLens() { return new TableLens; }
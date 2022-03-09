#include "CamHostPluginAPI.h"
#include <iostream>
#include <iomanip> 
#include <thread> // just for test big delay
#include <chrono> // std::chrono::seconds

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <memory>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

#include "../HydraCore/hydra_drv/cglobals.h"
#include "../HydraAPI/hydra_api/pugixml.hpp" // for XML
#include "../HydraAPI/hydra_api/HydraAPI.h"

class SimpleDOF : public IHostRaysAPI
{
public:
  SimpleDOF() { hr_qmc::init(table); m_globalCounter = 0; }
  
  void SetParameters(int a_width, int a_height, const float a_projInvMatrix[16], const wchar_t* a_camNodeText) override
  {
    m_fwidth  = float(a_width);
    m_fheight = float(a_height);
    memcpy(&m_projInv, a_projInvMatrix, sizeof(float4x4));

    m_doc.load_string(a_camNodeText);
    ReadParamsFromNode(m_doc.child(L"camera"));
  }

  void ReadParamsFromNode(pugi::xml_node a_camNode);

  void MakeRaysBlock(RayPart1* out_rayPosAndNear, RayPart2* out_rayDirAndFar, size_t in_blockSize, int passId) override;
  void AddSamplesContribution(float* out_color4f, const float* colors4f, size_t in_blockSize, uint32_t a_width, uint32_t a_height, int passId) override;
  void FinishRendering() override { std::cout << "SimpleDOF::FinishRendering is called" << std::endl; }

  pugi::xml_document m_doc;

  unsigned int table[hr_qmc::QRNG_DIMENSIONS][hr_qmc::QRNG_RESOLUTION];
  unsigned int m_globalCounter = 0;

  float m_fwidth  = 1024.0f;
  float m_fheight = 1024.0f;
  float4x4 m_projInv;

  float FOCAL_PLANE_DIST = 10.0f;
  float DOF_LENS_RADIUS  = 0.0f;
  bool  DOF_IS_ENABLED = false;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SimpleDOF::ReadParamsFromNode(pugi::xml_node a_camNode)
{
  if (a_camNode.child(L"enable_dof").text().empty())
    return;

  int hasDof = a_camNode.child(L"enable_dof").text().as_int();
  if (hasDof > 0)
  {
    DOF_IS_ENABLED  = true;
    DOF_LENS_RADIUS = a_camNode.child(L"dof_lens_radius").text().as_float();
    
    // compute 'FOCAL_PLANE_DIST' from camPos and lookAt parameters 
    //
    float3 camPos, camLookAt;
    {
      const wchar_t* camPosStr = a_camNode.child(L"position").text().as_string();
      const wchar_t* camLAtStr = a_camNode.child(L"look_at").text().as_string();
      if (std::wstring(camPosStr) != L"")
      {
        std::wstringstream input(camPosStr);
        input >> camPos.x >> camPos.y >> camPos.z;
      }
      if (std::wstring(camLAtStr) != L"")
      {
        std::wstringstream input(camLAtStr);
        input >> camLookAt.x >> camLookAt.y >> camLookAt.z;
      }
    }
    FOCAL_PLANE_DIST = length(camPos - camLookAt);
  }
  else
    DOF_IS_ENABLED = false;
}

static inline int myPackXY1616(int x, int y) { return (y << 16) | (x & 0x0000FFFF); }

void SimpleDOF::MakeRaysBlock(RayPart1* out_rayPosAndNear, RayPart2* out_rayDirAndFar, size_t in_blockSize, int passId)
{
  #pragma omp parallel for
  for(int i=0;i<in_blockSize;i++)
  {
    const float rndX = hr_qmc::rndFloat(m_globalCounter+i, 0, table[0]);
    const float rndY = hr_qmc::rndFloat(m_globalCounter+i, 1, table[0]);
    
    const float x    = m_fwidth*rndX; 
    const float y    = m_fheight*rndY;

    float3 ray_pos = float3(0,0,0);
    float3 ray_dir = EyeRayDir(x, y, m_fwidth, m_fheight, m_projInv);

    if (DOF_IS_ENABLED) // dof is enabled
    {
      const float lenzX = hr_qmc::rndFloat(m_globalCounter+i, 2, table[0]);
      const float lenzY = hr_qmc::rndFloat(m_globalCounter+i, 3, table[0]);

      const float tFocus         = FOCAL_PLANE_DIST / (-ray_dir.z);
      const float3 focusPosition = ray_pos + ray_dir*tFocus;
      const float2 xy            = DOF_LENS_RADIUS*2.0f*MapSamplesToDisc(float2(lenzX - 0.5f, lenzY - 0.5f));
      ray_pos.x += xy.x;
      ray_pos.y += xy.y;
  
      ray_dir = normalize(focusPosition - ray_pos);
    }


    RayPart1 p1;
    p1.origin[0]   = ray_pos.x;
    p1.origin[1]   = ray_pos.y;
    p1.origin[2]   = ray_pos.z;
    p1.xyPosPacked = myPackXY1616(int(x), int(y));
   
    RayPart2 p2;
    p2.direction[0] = ray_dir.x;
    p2.direction[1] = ray_dir.y;
    p2.direction[2] = ray_dir.z;
    p2.dummy        = 0.0f;
    
    out_rayPosAndNear[i] = p1;
    out_rayDirAndFar [i] = p2;
  }

  //std::this_thread::sleep_for(std::chrono::milliseconds(50)); // test big delay

  m_globalCounter += unsigned(in_blockSize);
} 

void SimpleDOF::AddSamplesContribution(float* out_color4f, const float* colors4f, size_t in_blockSize, uint32_t a_width, uint32_t a_height, int passId)
{
  float4*       out_color = (float4*)out_color4f;
  const float4* colors    = (const float4*)colors4f;
  
  for (int i = 0; i < in_blockSize; i++)
  {
    const auto color = colors[i];
    const uint32_t packedIndex = as_int(color.w);
    const int x      = (packedIndex & 0x0000FFFF);         ///<! extract x position from color.w
    const int y      = (packedIndex & 0xFFFF0000) >> 16;   ///<! extract y position from color.w
    const int offset = y*a_width + x;

    if (x >= 0 && y >= 0 && x < a_width && y < a_height)
    {
      out_color[offset].x += color.x;
      out_color[offset].y += color.y;
      out_color[offset].z += color.z;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
IHostRaysAPI* CreateTableLens();

IHostRaysAPI* MakeHostRaysEmitter(int a_pluginId) ///<! you replace this function or make your own ... the example will be provided
{
  if(a_pluginId == 0)
    return nullptr;

  std::cout << "[MakeHostRaysEmitter]: create plugin #" << a_pluginId << std::endl;
  if(a_pluginId == 2)
    return CreateTableLens();
  else
    return new SimpleDOF();
}

void DeleteRaysEmitter(IHostRaysAPI* pObject) { delete pObject; }

#ifdef WIN32

#include <windows.h>

std::string ws2s(const std::wstring& s)
{
  int len;
  int slength = (int)s.length() + 1;
  len = WideCharToMultiByte(CP_ACP, 0, s.c_str(), slength, 0, 0, 0, 0);
  char* buf = new char[len];
  WideCharToMultiByte(CP_ACP, 0, s.c_str(), slength, buf, len, 0, 0);
  std::string r(buf);
  delete[] buf;
  return r;
}

std::wstring s2ws(const std::string& s)
{
  int len;
  int slength = (int)s.length() + 1;
  len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
  wchar_t* buf = new wchar_t[len];
  MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
  std::wstring r(buf);
  delete[] buf;
  return r;
}
#else

#include <codecvt>

std::wstring s2ws(const std::string& str)
{
  using convert_typeX = std::codecvt_utf8<wchar_t>;
  std::wstring_convert<convert_typeX, wchar_t> converterX;
  
  return converterX.from_bytes(str);
}

std::string ws2s(const std::wstring& wstr)
{
  using convert_typeX = std::codecvt_utf8<wchar_t>;
  std::wstring_convert<convert_typeX, wchar_t> converterX;
  
  return converterX.to_bytes(wstr);
}

#endif
#include "RenderingParams.h"
#include <cinder/CinderGlm.h>
using namespace glm;

float RenderingParams::gammaValue = 2.2f;
float RenderingParams::exposureValue = 1.0f;
bool RenderingParams::fxaa = true;
bool RenderingParams::diffuseShading = true;
bool RenderingParams::shadows = true;

float RenderingParams::GetExposure() 
{
    return exposureValue;
}

void RenderingParams::SetExposure(const float exposure)
{
    exposureValue = min(max(epsilon<float>(), exposure), 8.0f);
}

float RenderingParams::GetGamma()
{
    return gammaValue;
}

void RenderingParams::SetGamma(const float gamma)
{
    gammaValue = min(max(epsilon<float>(), gamma), 10.0f);
}

void RenderingParams::FXAAEnabled(const bool enabled)
{
    fxaa = enabled;
}

bool RenderingParams::FXAAEnabled()
{
    return fxaa;
}

void RenderingParams::DiffuseShadingEnabled(const bool enabled)
{
    diffuseShading = enabled;
}

bool RenderingParams::DiffuseShadingEnabled()
{
    return diffuseShading;
}

void RenderingParams::ShadowsEnabled(const bool enabled)
{
    shadows = enabled;
}

bool RenderingParams::ShadowsEnabled()
{
    return shadows;
}

#pragma once
#include "cinder/gl/gl.h"
#include <glm/detail/type_mat.hpp>
#include <glm/detail/type_mat.hpp>

class Volume3D
{
public:
    glm::vec3 ratios;
    float stepScale;

    void createFromFile(const glm::vec3& dimensions, const glm::vec3& ratios, const std::string filepath, bool is16Bits = false);
    void drawFrontCubeFace() const;
    void drawBackCubeFace() const;
    void drawVolume() const;
    explicit Volume3D();
    ~Volume3D();
private:
    // cube for positions
    ci::gl::BufferObjRef verticesBuffer;
    ci::gl::BufferObjRef indicesBuffer;
    ci::gl::VaoRef vertexArrayObject;
    // volume texture
    ci::gl::Texture3dRef volumeTexture;
    // front cube fbo
    ci::gl::FboRef frontFbo;
    ci::gl::RenderbufferRef frontDepth;
    // back cube fbo
    ci::gl::FboRef backFbo;
    ci::gl::RenderbufferRef backDepth;
    // draw positions shader
    cinder::gl::GlslProgRef positionsShader;
    // volume raycast
    cinder::gl::GlslProgRef raycastShader;
    // raycast parameters
    glm::vec3 dimensions;
    glm::vec3 stepSize;
    float maxSize;

    bool isDrawable;

    void createCubeVbo();
    void readVolumeFromFile8(const glm::vec3& dimensions, const std::string filepath);
    void readVolumeFromFile16(const glm::vec3& dimensions, const std::string filepath);
    void createFbos();
};

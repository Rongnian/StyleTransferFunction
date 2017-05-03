#include "RaycastVolume.h"
#include <cinder/app/AppBase.h>
#include <cinder/Log.h>
#include "TransferFunction.h"
using namespace ci;
using namespace glm;

void RaycastVolume::createFullscreenQuad()
{
    fsQuadMesh = TriMesh::create(geom::Rect());

    fsQuadVao = gl::Vao::create();
    // bind vertex buffer object
    gl::ScopedVao scopedVao(fsQuadVao);
    {
        fsQuadVerticesBuffer = gl::Vbo::create(GL_ARRAY_BUFFER, sizeof(vec2) * fsQuadMesh->getNumVertices(),
                                                              fsQuadMesh->getPositions<2>(), GL_STATIC_DRAW);
        {
            gl::ScopedBuffer scopedBuffer(fsQuadVerticesBuffer);
            gl::vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, static_cast<const GLvoid*>(nullptr));
            gl::enableVertexAttribArray(0);
        }
        fsQuadTexcoordBuffer = gl::Vbo::create(GL_ARRAY_BUFFER, sizeof(vec2) * fsQuadMesh->getNumVertices(),
                                                              fsQuadMesh->getTexCoords0<2>(), GL_STATIC_DRAW);
        {
            gl::ScopedBuffer scopedBuffer(fsQuadTexcoordBuffer);
            gl::vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, static_cast<const GLvoid*>(nullptr));
            gl::enableVertexAttribArray(1);
        }
        fsQuadIndicesBuffer = gl::Vbo::create<uint32_t>(GL_ELEMENT_ARRAY_BUFFER, fsQuadMesh->getIndices(), GL_STATIC_DRAW);
        {
            gl::ScopedBuffer scopedBuffer(fsQuadIndicesBuffer);
        }
    }
}

RaycastVolume::RaycastVolume() : aspectRatios(1), scaleFactor(vec3(1)), stepScale(1), enableDiffuseShading(true)
{
    // positions shader
    positionsShader = gl::GlslProg::create(gl::GlslProg::Format()
        .vertex(loadFile("shaders/positions.vert"))
        .fragment(loadFile("shaders/positions.frag")));
    // raycast shader
    raycastShader = gl::GlslProg::create(gl::GlslProg::Format()
        .vertex(loadFile("shaders/raycast.vert"))
        .fragment(loadFile("shaders/raycast.frag")));
    raycastShaderGBuffer = gl::GlslProg::create(gl::GlslProg::Format()
        .vertex(loadFile("shaders/raycast.vert"))
        .fragment(loadFile("shaders/raycast_gbuffer.frag")));
    // deferred light pass
    lightBuffer = gl::GlslProg::create(gl::GlslProg::Format()
        .vertex(loadFile("shaders/fs_quad.vert"))
        .fragment(loadFile("shaders/light_buffer.frag")));
    // histogram calculation 
    histogramCompute = gl::GlslProg::create(gl::GlslProg::Format()
        .compute(loadFile("shaders/histogram.comp")));
    // gradient computation
    gradientsCompute = gl::GlslProg::create(gl::GlslProg::Format()
        .compute(loadFile("shaders/gradients.comp")));
    smoothGradientsCompute = gl::GlslProg::create(gl::GlslProg::Format()
        .compute(loadFile("shaders/smooth_gradients.comp")));
    // noise texture to reduce volume banding artifacts
    noiseTexture = gl::Texture2d::create(loadImage("images/noise.png"), gl::Texture2d::Format()
                                         .wrapS(GL_REPEAT)
                                         .wrapT(GL_REPEAT)
                                         .magFilter(GL_NEAREST)
                                         .minFilter(GL_NEAREST));
    // full screen quad
    createFullscreenQuad();
    // create clockwise bbox for volume rendering
    createCubeVbo();
    // create frame buffer object
    resizeFbos();
}

RaycastVolume::~RaycastVolume() {}

vec3 RaycastVolume::centerPoint() const
{
    return vec3(0.5) * scaleFactor;
}

const float &RaycastVolume::getStepScale() const
{
    return stepScale;
}

void RaycastVolume::setStepScale(const float& value)
{
    stepScale = max(value, 0.1f);
}

const vec3 &RaycastVolume::getAspectRatios() const
{
    return aspectRatios;
}

void RaycastVolume::setAspectratios(const vec3& value)
{
    aspectRatios = max(value, vec3(0));
    // volume scale
    scaleFactor = vec3(1) / ((vec3(1) * maxSize) / (dimensions * aspectRatios));
}

void RaycastVolume::createCubeVbo()
{
    cubeMesh = TriMesh::create(geom::Cube());

    // move cube to 0 - 1 coordinates
    for (auto& pos : cubeMesh->getBufferPositions()) { pos += .5; }

    cubeVao = gl::Vao::create();
    // bind vertex buffer object
    gl::ScopedVao scopedVao(cubeVao);
    {
        cubeVerticesBuffer = gl::Vbo::create(GL_ARRAY_BUFFER, sizeof(vec3) * cubeMesh->getNumVertices(),
                                                            cubeMesh->getPositions<3>(), GL_STATIC_DRAW);
        {
            gl::ScopedBuffer scopedBuffer(cubeVerticesBuffer);
            gl::vertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, static_cast<const GLvoid*>(nullptr));
            gl::enableVertexAttribArray(0);
        }
        cubeIndicesBuffer = gl::Vbo::create<uint32_t>(GL_ELEMENT_ARRAY_BUFFER, cubeMesh->getIndices(), GL_STATIC_DRAW);
        {
            gl::ScopedBuffer scopedBuffer(cubeIndicesBuffer);
        }
    }
}

void RaycastVolume::readVolumeFromFile8(const std::string filepath)
{
    // open file and read
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);

    // error opening given filepath
    if (!file.good()) return;

    // determine volume size
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    // reserve space for data
    std::vector<uint8_t> buffer(size);

    if (file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        // create 3D texture
        auto format = gl::Texture3d::Format().magFilter(GL_LINEAR)
                                             .minFilter(GL_LINEAR)
                                             .wrapS(GL_CLAMP_TO_BORDER)
                                             .wrapR(GL_CLAMP_TO_BORDER)
                                             .wrapT(GL_CLAMP_TO_BORDER);
        format.setDataType(GL_UNSIGNED_BYTE);
        format.setInternalFormat(GL_RED);
        format.setSwizzleMask(GL_RED, GL_RED, GL_RED, GL_RED);
        volumeTexture = gl::Texture3d::create(buffer.data(), GL_RED,
                                              dimensions.x, dimensions.y, dimensions.z, format);
        // finally has drawable data
        isDrawable = true;
    }

    file.close();
}

void RaycastVolume::readVolumeFromFile16(const std::string filepath)
{
    // open file and read
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);

    // error opening given filepath
    if (!file.good()) return;

    // determine volume size
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    // reserve space for data
    std::vector<uint16_t> buffer(size);

    if (file.read(reinterpret_cast<char*>(buffer.data()), size))
    {
        // create 3D texture
        auto format = gl::Texture3d::Format().magFilter(GL_LINEAR)
                                             .minFilter(GL_LINEAR)
                                             .wrapS(GL_CLAMP_TO_BORDER)
                                             .wrapR(GL_CLAMP_TO_BORDER)
                                             .wrapT(GL_CLAMP_TO_BORDER);
        format.setDataType(GL_UNSIGNED_SHORT);
        format.setInternalFormat(GL_RED);
        format.setSwizzleMask(GL_RED, GL_RED, GL_RED, GL_RED);
        volumeTexture = gl::Texture3d::create(buffer.data(), GL_RED,
                                              dimensions.x, dimensions.y, dimensions.z, format);
        // finally has drawable data
        isDrawable = true;
    }

    file.close();
}

void RaycastVolume::loadFromFile(const vec3& dimensions, const vec3& ratios, const std::string filepath,
                                 bool is16Bits)
{
    // compute step size and number of iterations for the given volume dimensions
    this->dimensions = dimensions;
    maxSize = max(dimensions.x, max(dimensions.y, dimensions.z));
    stepSize = vec3(1.0f / (dimensions.x * (maxSize / dimensions.x)),
                    1.0f / (dimensions.y * (maxSize / dimensions.y)),
                    1.0f / (dimensions.z * (maxSize / dimensions.z)));
    setAspectratios(ratios);
    // create volume texture
    is16Bits ? readVolumeFromFile16(filepath) : readVolumeFromFile8(filepath);
    // histogram compute
    extractHistogram();
    // gradients
    generateGradients();
}

void RaycastVolume::drawCubeFaces()
{
    // draw front face
    {
        gl::ScopedFramebuffer scopedFramebuffer(frontFbo);
        gl::ScopedViewport scopedViewport(ivec2(0), frontFbo->getSize());
        gl::ScopedFaceCulling faceCulling(true, GL_BACK);
        // draw front face cube positions to render target 
        gl::clear();
        positionsShader->bind();
        positionsShader->uniform("scaleFactor", scaleFactor);
        positionsShader->uniform("renderingFront", true);
        gl::setDefaultShaderVars();
        gl::drawElements(gl::toGl(cubeMesh->getPrimitive()), cubeMesh->getNumIndices(),
                         GL_UNSIGNED_INT, static_cast<GLuint *>(nullptr));
    }
    // draw back face
    {
        gl::ScopedFramebuffer scopedFramebuffer(backFbo);
        gl::ScopedViewport scopedViewport(ivec2(0), backFbo->getSize());
        gl::ScopedFaceCulling faceCulling(true, GL_FRONT);
        // draw back face cube positions to render target
        gl::clear();
        gl::drawElements(gl::toGl(cubeMesh->getPrimitive()), cubeMesh->getNumIndices(),
                         GL_UNSIGNED_INT, static_cast<GLuint *>(nullptr));
    }
}

void RaycastVolume::drawVolume(bool deferredPath)
{
    if (!isDrawable) return;

    // volume raycast
    {
        gl::ScopedMatrices scopedMatrices;
        gl::ScopedVao scopedCubeVao(cubeVao);
        gl::ScopedBuffer scopedCubeIndicesBuffer(cubeIndicesBuffer);

        // set model matrix for model
        gl::rotate(modelRotation);
        gl::translate(modelPosition);

        // draw positions
        drawCubeFaces();

        // bind  textures
        gl::ScopedTextureBind frontTex(frontTexture, 0);
        gl::ScopedTextureBind backTex(backTexture, 1);
        gl::ScopedTextureBind volumeTex(volumeTexture, 2);
        gl::ScopedTextureBind trasferTex(transferFunction->get1DTexture(), 4);
        gl::ScopedTextureBind noiseTex(noiseTexture, 5);

        // shader program to use
        auto shader = deferredPath ? raycastShaderGBuffer : raycastShader;
        gl::ScopedGlslProg scopedProg(shader);
        gl::setDefaultShaderVars();

        // raycast parameters
        shader->uniform("threshold", transferFunction->getThreshold());
        shader->uniform("scaleFactor", scaleFactor);
        shader->uniform("stepSize", stepSize * stepScale);
        shader->uniform("iterations", static_cast<int>(maxSize * (1.0f / stepScale) * 2.0f));

        // alpha blending
        gl::ScopedBlend blend(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // raycast volume
        if (!deferredPath)
        {
            gl::clear();
            // specific texture for forward
            gl::ScopedTextureBind gradientTex(gradientTexture, 3);
            // lighting
            shader->uniform("diffuseShading", enableDiffuseShading);
            shader->uniform("light.direction", light.direction);
            shader->uniform("light.ambient", light.ambient);
            shader->uniform("light.diffuse", light.diffuse);

            // draw cube
            gl::drawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, static_cast<GLuint *>(nullptr));
        }
        else
        {
            const gl::ScopedFramebuffer scopedFramebuffer(gBuffer);
            const gl::ScopedViewport scopedViewport(ivec2(0), gBuffer->getSize());
            gl::clear();

            // draw cube
            gl::drawElements(gl::toGl(cubeMesh->getPrimitive()), cubeMesh->getNumIndices(),
                             GL_UNSIGNED_INT, static_cast<GLuint *>(nullptr));
        }
    }
    // light pass
    if (deferredPath)
    {
        gl::ScopedGlslProg scopedLightProg(lightBuffer);
        gl::ScopedVao scopedFsQuadVao(fsQuadVao);
        gl::ScopedBuffer fsQuadIndices(fsQuadIndicesBuffer);
        gl::ScopedViewport scopedViewport(ivec2(0), app::toPixels(app::getWindowSize()));
        gl::ScopedMatrices scopedMatrices;

        // no depth read or write on composition
        gl::disableDepthRead();
        gl::disableDepthWrite();
        gl::clear();

        // scale and center quad to fit whole screen
        gl::translate(app::getWindowCenter());
        gl::scale(app::getWindowSize());
        gl::setDefaultShaderVars();

        // draw fs quad
        gl::drawElements(gl::toGl(fsQuadMesh->getPrimitive()), fsQuadMesh->getNumIndices(),
                         GL_UNSIGNED_INT, static_cast<GLuint *>(nullptr));
    }
}

void RaycastVolume::resizeFbos()
{
    gl::Texture2d::Format dataFormat = gl::Texture2d::Format().internalFormat(GL_RGB16F)
                                                              .magFilter(GL_NEAREST)
                                                              .minFilter(GL_NEAREST)
                                                              .wrap(GL_REPEAT)
                                                              .dataType(GL_FLOAT);

    gl::Texture2d::Format colorFormat = gl::Texture2d::Format().internalFormat(GL_RGBA8)
                                                               .magFilter(GL_NEAREST)
                                                               .minFilter(GL_NEAREST)
                                                               .wrap(GL_CLAMP_TO_EDGE);

    const ivec2 winSize = app::getWindowSize();
    const int32_t h = winSize.y;
    const int32_t w = winSize.x;

    try
    {
        // cube positions rendering
        gl::Fbo::Format frontFormat, backFormat;
        frontTexture = gl::Texture2d::create(w, h, dataFormat);
        backTexture = gl::Texture2d::create(w, h, dataFormat);

        // front fbo
        frontFormat.attachment(GL_COLOR_ATTACHMENT0, frontTexture);
        frontFormat.depthBuffer();
        frontFbo = gl::Fbo::create(w, h, frontFormat);

        // back fbo
        backFormat.attachment(GL_COLOR_ATTACHMENT0, backTexture);
        backFormat.depthBuffer();
        backFbo = gl::Fbo::create(w, h, backFormat);

        // deferred rendering mode gbuffer
        gl::Fbo::Format gBufferFormat;
        gBufferFormat.colorTexture(colorFormat);
        gBufferFormat.depthTexture();
        gBuffer = gl::Fbo::create(w, h, gBufferFormat);
    }
    catch (const Exception& e)
    {
        CI_LOG_EXCEPTION("Fbo/Renderbuffer create", e);
    }
}

void RaycastVolume::extractHistogram()
{
    std::array<uint32_t, 256> histogramData = {0};
    // create shader storage buffer
    histogramSsbo = gl::Ssbo::create(256 * sizeof(uint32_t), histogramData.data(), GL_DYNAMIC_COPY);
    histogramSsbo->bindBase(1);

    // compute histogram
    {
        // bind histogram compute shader
        histogramCompute->bind();
        // volume texture
        glBindImageTexture(0, volumeTexture->getId(), 0, true, 0, GL_READ_ONLY, GL_R8UI);
        bindBufferBase(histogramSsbo->getTarget(), 1, histogramSsbo);
        // compute histogram
        gl::dispatchCompute(1, 1, 1);
        // block to ensure completion
        gl::memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    // extract histogram values
    uint32_t* ssboValues = reinterpret_cast<uint32_t*>(histogramSsbo->map(GL_WRITE_ONLY));
    memcpy(histogramData.data(), ssboValues, 256 * sizeof(uint32_t));
    histogramSsbo->unmap();
    // convert to normalized floating values
    auto maxValue = max_element(histogramData.begin(), histogramData.end());

    // normalize and insert values
    for (int i = 0; i < 256; i++)
    {
        histogram[i] = static_cast<float>(histogramData[i]) / *maxValue;
    }
}

void RaycastVolume::generateGradients()
{
    auto format = gl::Texture3d::Format().magFilter(GL_LINEAR)
                                         .minFilter(GL_LINEAR)
                                         .wrapS(GL_CLAMP_TO_BORDER)
                                         .wrapR(GL_CLAMP_TO_BORDER)
                                         .wrapT(GL_CLAMP_TO_BORDER)
                                         .internalFormat(GL_RG16F);
    format.setDataType(GL_FLOAT);
    gradientTexture = gl::Texture3d::create(dimensions.x, dimensions.y, dimensions.z, format);

    // compute gradients
    {
        gradientsCompute->bind();
        // pass textures
        glBindImageTexture(0, volumeTexture->getId(), 0, true, 0, GL_READ_ONLY, GL_R8UI);
        glBindImageTexture(1, gradientTexture->getId(), 0, true, 0, GL_WRITE_ONLY, GL_RG16F);
        // compute gradients
        gl::dispatchCompute(ceil(dimensions.x / 8), ceil(dimensions.y / 8), ceil(dimensions.z / 8));
    }
    // smooth gradients
    {
        smoothGradientsCompute->bind();
        // pass textures
        glBindImageTexture(0, gradientTexture->getId(), 0, true, 0, GL_READ_WRITE, GL_RG16F);
        // compute gradients
        gl::dispatchCompute(ceil(dimensions.x / 8), ceil(dimensions.y / 8), ceil(dimensions.z / 8));
    }
}

const std::array<float, 256> &RaycastVolume::getHistogram() const
{
    return histogram;
}

void RaycastVolume::setTransferFunction(const std::shared_ptr<TransferFunction>& transferFunction)
{
    this->transferFunction = transferFunction;
}

void RaycastVolume::diffuseShading(bool enable)
{
    this->enableDiffuseShading = enable;
}

void RaycastVolume::setLight(vec3 direction, vec3 ambient, vec3 diffuse)
{
    light.direction = direction;
    light.ambient = ambient;
    light.diffuse = diffuse;
}

const Light &RaycastVolume::getLight() const { return light; }

const quat &RaycastVolume::getRotation() const { return modelRotation; }

void RaycastVolume::setRotation(const quat& rotation) { modelRotation = rotation; }

const vec3 &RaycastVolume::getPosition() const { return modelPosition; }

void RaycastVolume::setPosition(const vec3& position) { modelPosition = position; }

/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the Qt Shader Tools module
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qspirvshader_p.h"
#include <QtGui/private/qshaderdescription_p_p.h>
#include <QFile>
#include <QDebug>

#include <SPIRV/SPVRemapper.h>

#include <spirv_glsl.hpp>
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>

QT_BEGIN_NAMESPACE

struct QSpirvShaderPrivate
{
    ~QSpirvShaderPrivate();

    void createGLSLCompiler();
    void processResources();

    QShaderDescription::InOutVariable inOutVar(const spirv_cross::Resource &r);
    QShaderDescription::BlockVariable blockVar(uint32_t typeId,
                                               uint32_t memberIdx,
                                               uint32_t memberTypeId);

    void remapErrorHandler(const std::string &s);
    void remapLogHandler(const std::string &s);

    QByteArray ir;
    QShaderDescription shaderDescription;

    spirv_cross::CompilerGLSL *glslGen = nullptr;
    spirv_cross::CompilerHLSL *hlslGen = nullptr;
    spirv_cross::CompilerMSL *mslGen = nullptr;

    QString spirvCrossErrorMsg;
    QString remapErrorMsg;
};

QSpirvShaderPrivate::~QSpirvShaderPrivate()
{
    delete mslGen;
    delete hlslGen;
    delete glslGen;
}

void QSpirvShaderPrivate::createGLSLCompiler()
{
    delete glslGen;
    glslGen = new spirv_cross::CompilerGLSL(reinterpret_cast<const uint32_t *>(ir.constData()), ir.size() / 4);
}

static QShaderDescription::VariableType matVarType(const spirv_cross::SPIRType &t, QShaderDescription::VariableType compType)
{
    switch (t.columns) {
    case 2:
        return QShaderDescription::VariableType(compType + 4 + (t.vecsize == 3 ? 1 : t.vecsize == 4 ? 2 : 0));
    case 3:
        return QShaderDescription::VariableType(compType + 7 + (t.vecsize == 2 ? 1 : t.vecsize == 4 ? 2 : 0));
    case 4:
        return QShaderDescription::VariableType(compType + 10 + (t.vecsize == 2 ? 1 : t.vecsize == 3 ? 2 : 0));
    default:
        return QShaderDescription::Unknown;
    }
}

static QShaderDescription::VariableType vecVarType(const spirv_cross::SPIRType &t, QShaderDescription::VariableType compType)
{
    switch (t.vecsize) {
    case 1:
        return compType;
    case 2:
        return QShaderDescription::VariableType(compType + 1);
    case 3:
        return QShaderDescription::VariableType(compType + 2);
    case 4:
        return QShaderDescription::VariableType(compType + 3);
    default:
        return QShaderDescription::Unknown;
    }
}

static QShaderDescription::VariableType sampledImageVarType(const spirv_cross::SPIRType &t)
{
    switch (t.image.dim) {
    case spv::Dim1D:
        return t.image.arrayed ? QShaderDescription::Sampler1DArray : QShaderDescription::Sampler1D;
    case spv::Dim2D:
        return t.image.arrayed
                ? (t.image.ms ? QShaderDescription::Sampler2DMSArray : QShaderDescription::Sampler2DArray)
                : (t.image.ms ? QShaderDescription::Sampler2DMS : QShaderDescription::Sampler2D);
    case spv::Dim3D:
        return t.image.arrayed ? QShaderDescription::Sampler3DArray : QShaderDescription::Sampler3D;
    case spv::DimCube:
        return t.image.arrayed ? QShaderDescription::SamplerCubeArray : QShaderDescription::SamplerCube;
    case spv::DimRect:
        return QShaderDescription::SamplerRect;
    case spv::DimBuffer:
        return QShaderDescription::SamplerBuffer;
    default:
        return QShaderDescription::Unknown;
    }
}

static QShaderDescription::VariableType imageVarType(const spirv_cross::SPIRType &t)
{
    switch (t.image.dim) {
    case spv::Dim1D:
        return t.image.arrayed ? QShaderDescription::Image1DArray : QShaderDescription::Image1D;
    case spv::Dim2D:
        return t.image.arrayed
                ? (t.image.ms ? QShaderDescription::Image2DMSArray : QShaderDescription::Image2DArray)
                : (t.image.ms ? QShaderDescription::Image2DMS : QShaderDescription::Image2D);
    case spv::Dim3D:
        return t.image.arrayed ? QShaderDescription::Image3DArray : QShaderDescription::Image3D;
    case spv::DimCube:
        return t.image.arrayed ? QShaderDescription::ImageCubeArray : QShaderDescription::ImageCube;
    case spv::DimRect:
        return QShaderDescription::ImageRect;
    case spv::DimBuffer:
        return QShaderDescription::ImageBuffer;
    default:
        return QShaderDescription::Unknown;
    }
}

static QShaderDescription::VariableType varType(const spirv_cross::SPIRType &t)
{
    QShaderDescription::VariableType vt = QShaderDescription::Unknown;
    switch (t.basetype) {
    case spirv_cross::SPIRType::Float:
        vt = t.columns > 1 ? matVarType(t, QShaderDescription::Float) : vecVarType(t, QShaderDescription::Float);
        break;
    case spirv_cross::SPIRType::Double:
        vt = t.columns > 1 ? matVarType(t, QShaderDescription::Double) : vecVarType(t, QShaderDescription::Double);
        break;
    case spirv_cross::SPIRType::UInt:
        vt = vecVarType(t, QShaderDescription::Uint);
        break;
    case spirv_cross::SPIRType::Int:
        vt = vecVarType(t, QShaderDescription::Int);
        break;
    case spirv_cross::SPIRType::Boolean:
        vt = vecVarType(t, QShaderDescription::Uint);
        break;
    case spirv_cross::SPIRType::SampledImage:
        vt = sampledImageVarType(t);
        break;
    case spirv_cross::SPIRType::Image:
        vt = imageVarType(t);
        break;
    case spirv_cross::SPIRType::Struct:
        vt = QShaderDescription::Struct;
        break;
    default:
        // can encounter types we do not (yet) handle, return Unknown for those
        break;
    }
    return vt;
}

QShaderDescription::InOutVariable QSpirvShaderPrivate::inOutVar(const spirv_cross::Resource &r)
{
    QShaderDescription::InOutVariable v;
    v.name = QString::fromStdString(r.name);

    const spirv_cross::SPIRType &t = glslGen->get_type(r.base_type_id);
    v.type = varType(t);

    if (glslGen->has_decoration(r.id, spv::DecorationLocation))
        v.location = glslGen->get_decoration(r.id, spv::DecorationLocation);

    if (glslGen->has_decoration(r.id, spv::DecorationBinding))
        v.binding = glslGen->get_decoration(r.id, spv::DecorationBinding);

    if (glslGen->has_decoration(r.id, spv::DecorationDescriptorSet))
        v.descriptorSet = glslGen->get_decoration(r.id, spv::DecorationDescriptorSet);

    if (t.basetype == spirv_cross::SPIRType::Image) {
        v.imageFormat = QShaderDescription::ImageFormat(t.image.format);

        // t.image.access is relevant for OpenCL kernels only so ignore.

        // No idea how to access the decorations like
        // DecorationNonReadable/Writable in a way that it returns the real
        // values (f.ex. has_decoration() on r.id or so is not functional). So
        // ignore these for now and pretend the image is read/write.
        v.imageFlags = 0;
    }

    return v;
}

QShaderDescription::BlockVariable QSpirvShaderPrivate::blockVar(uint32_t typeId,
                                                                uint32_t memberIdx,
                                                                uint32_t memberTypeId)
{
    QShaderDescription::BlockVariable v;
    v.name = QString::fromStdString(glslGen->get_member_name(typeId, memberIdx));

    const spirv_cross::SPIRType &memberType(glslGen->get_type(memberTypeId));
    v.type = varType(memberType);

    const spirv_cross::SPIRType &t = glslGen->get_type(typeId);
    v.offset = glslGen->type_struct_member_offset(t, memberIdx);
    v.size = int(glslGen->get_declared_struct_member_size(t, memberIdx));

    for (uint32_t dimSize : memberType.array)
        v.arrayDims.append(dimSize);

    if (glslGen->has_member_decoration(typeId, memberIdx, spv::DecorationArrayStride))
        v.arrayStride = glslGen->type_struct_member_array_stride(t, memberIdx);

    if (glslGen->has_member_decoration(typeId, memberIdx, spv::DecorationMatrixStride))
        v.matrixStride = glslGen->type_struct_member_matrix_stride(t, memberIdx);

    if (glslGen->has_member_decoration(typeId, memberIdx, spv::DecorationRowMajor))
        v.matrixIsRowMajor = true;

    if (v.type == QShaderDescription::Struct) {
        uint32_t memberMemberIdx = 0;
        for (uint32_t memberMemberType : memberType.member_types) {
            v.structMembers.append(blockVar(memberType.self, memberMemberIdx, memberMemberType));
            ++memberMemberIdx;
        }
    }

    return v;
}

void QSpirvShaderPrivate::processResources()
{
    shaderDescription = QShaderDescription();
    QShaderDescriptionPrivate *dd = QShaderDescriptionPrivate::get(&shaderDescription);

    spirv_cross::ShaderResources resources = glslGen->get_shader_resources();

    for (const spirv_cross::Resource &r : resources.stage_inputs) {
        const QShaderDescription::InOutVariable v = inOutVar(r);
        if (v.type != QShaderDescription::Unknown)
            dd->inVars.append(v);
    }

    for (const spirv_cross::Resource &r : resources.stage_outputs) {
        const QShaderDescription::InOutVariable v = inOutVar(r);
        if (v.type != QShaderDescription::Unknown)
            dd->outVars.append(v);
    }

    // uniform blocks map to either a uniform buffer or a plain struct
    for (const spirv_cross::Resource &r : resources.uniform_buffers) {
        const spirv_cross::SPIRType &t = glslGen->get_type(r.base_type_id);
        QShaderDescription::UniformBlock block;
        block.blockName = QString::fromStdString(r.name);
        block.structName = QString::fromStdString(glslGen->get_name(r.id));
        block.size = int(glslGen->get_declared_struct_size(t));
        if (glslGen->has_decoration(r.id, spv::DecorationBinding))
            block.binding = glslGen->get_decoration(r.id, spv::DecorationBinding);
        if (glslGen->has_decoration(r.id, spv::DecorationDescriptorSet))
            block.descriptorSet = glslGen->get_decoration(r.id, spv::DecorationDescriptorSet);
        uint32_t idx = 0;
        for (uint32_t memberTypeId : t.member_types) {
            const QShaderDescription::BlockVariable v = blockVar(r.base_type_id, idx, memberTypeId);
            ++idx;
            if (v.type != QShaderDescription::Unknown)
                block.members.append(v);
        }
        dd->uniformBlocks.append(block);
    }

    // push constant blocks map to a plain GLSL struct regardless of version
    for (const spirv_cross::Resource &r : resources.push_constant_buffers) {
        const spirv_cross::SPIRType &t = glslGen->get_type(r.base_type_id);
        QShaderDescription::PushConstantBlock block;
        block.name = QString::fromStdString(glslGen->get_name(r.id));
        block.size = int(glslGen->get_declared_struct_size(t));
        uint32_t idx = 0;
        for (uint32_t memberTypeId : t.member_types) {
            const QShaderDescription::BlockVariable v = blockVar(r.base_type_id, idx, memberTypeId);
            ++idx;
            if (v.type != QShaderDescription::Unknown)
                block.members.append(v);
        }
        dd->pushConstantBlocks.append(block);
    }

    for (const spirv_cross::Resource &r : resources.storage_buffers) {
        const spirv_cross::SPIRType &t = glslGen->get_type(r.base_type_id);
        QShaderDescription::StorageBlock block;
        block.blockName = QString::fromStdString(r.name);
        block.instanceName = QString::fromStdString(glslGen->get_name(r.id));
        block.knownSize = int(glslGen->get_declared_struct_size(t));
        if (glslGen->has_decoration(r.id, spv::DecorationBinding))
            block.binding = glslGen->get_decoration(r.id, spv::DecorationBinding);
        if (glslGen->has_decoration(r.id, spv::DecorationDescriptorSet))
            block.descriptorSet = glslGen->get_decoration(r.id, spv::DecorationDescriptorSet);
        uint32_t idx = 0;
        for (uint32_t memberTypeId : t.member_types) {
            const QShaderDescription::BlockVariable v = blockVar(r.base_type_id, idx, memberTypeId);
            ++idx;
            if (v.type != QShaderDescription::Unknown)
                block.members.append(v);
        }
        dd->storageBlocks.append(block);
    }

    for (const spirv_cross::Resource &r : resources.sampled_images) {
        const QShaderDescription::InOutVariable v = inOutVar(r);
        if (v.type != QShaderDescription::Unknown)
            dd->combinedImageSamplers.append(v);
    }

    for (const spirv_cross::Resource &r : resources.storage_images) {
        const QShaderDescription::InOutVariable v = inOutVar(r);
        if (v.type != QShaderDescription::Unknown)
            dd->storageImages.append(v);
    }
}

QSpirvShader::QSpirvShader()
    : d(new QSpirvShaderPrivate)
{
}

QSpirvShader::~QSpirvShader()
{
    delete d;
}

void QSpirvShader::setFileName(const QString &fileName)
{
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("QSpirvShader: Failed to open %s", qPrintable(fileName));
        return;
    }
    setDevice(&f);
}

void QSpirvShader::setDevice(QIODevice *device)
{
    d->ir = device->readAll();
    d->createGLSLCompiler();
    d->processResources();
}

void QSpirvShader::setSpirvBinary(const QByteArray &spirv)
{
    d->ir = spirv;
    d->createGLSLCompiler();
    d->processResources();
}

QShaderDescription QSpirvShader::shaderDescription() const
{
    return d->shaderDescription;
}

void QSpirvShaderPrivate::remapErrorHandler(const std::string &s)
{
    if (!remapErrorMsg.isEmpty())
        remapErrorMsg.append(QLatin1Char('\n'));
    remapErrorMsg.append(QString::fromStdString(s));
}

void QSpirvShaderPrivate::remapLogHandler(const std::string &)
{
}

QByteArray QSpirvShader::strippedSpirvBinary(StripFlags flags, QString *errorMessage) const
{
    if (d->ir.isEmpty())
        return QByteArray();

    spv::spirvbin_t b;

    d->remapErrorMsg.clear();
    b.registerErrorHandler(std::bind(&QSpirvShaderPrivate::remapErrorHandler, d, std::placeholders::_1));
    b.registerLogHandler(std::bind(&QSpirvShaderPrivate::remapLogHandler, d, std::placeholders::_1));

    const uint32_t opts = flags.testFlag(Remap) ? spv::spirvbin_t::DO_EVERYTHING : spv::spirvbin_t::STRIP;

    std::vector<uint32_t> v;
    v.resize(d->ir.size() / 4);
    memcpy(v.data(), d->ir.constData(), d->ir.size());

    b.remap(v, opts);

    if (!d->remapErrorMsg.isEmpty()) {
        if (errorMessage)
            *errorMessage = d->remapErrorMsg;
        return QByteArray();
    }

    return QByteArray(reinterpret_cast<const char *>(v.data()), int(v.size()) * 4);
}

QByteArray QSpirvShader::translateToGLSL(int version, GlslFlags flags) const
{
    d->spirvCrossErrorMsg.clear();

    try {
        // create a new instance every time since option handling seem to be problematic
        // (won't pick up new options on the second and subsequent compile())
        d->createGLSLCompiler();

        spirv_cross::CompilerGLSL::Options options;
        options.version = version;
        options.es = flags.testFlag(GlslEs);
        options.vertex.fixup_clipspace = flags.testFlag(FixClipSpace);
        options.fragment.default_float_precision = flags.testFlag(FragDefaultMediump)
                ? spirv_cross::CompilerGLSL::Options::Mediump
                : spirv_cross::CompilerGLSL::Options::Highp;
        // The gl backend of QRhi is not prepared for UBOs atm. Have a uniform (heh)
        // behavior regardless of the GLSL version.
        options.emit_uniform_buffer_as_plain_uniforms = true;
        d->glslGen->set_common_options(options);

        const std::string glsl = d->glslGen->compile();

        QByteArray src = QByteArray::fromStdString(glsl);

        // Fix it up by adding #extension GL_ARB_separate_shader_objects : require
        // as well in order to make Mesa and perhaps others happy.
        const QByteArray searchStr = QByteArrayLiteral("#extension GL_ARB_shading_language_420pack : require\n#endif\n");
        int pos = src.indexOf(searchStr);
        if (pos >= 0) {
            src.insert(pos + searchStr.count(), QByteArrayLiteral("#ifdef GL_ARB_separate_shader_objects\n"
                                                                  "#extension GL_ARB_separate_shader_objects : require\n"
                                                                  "#endif\n"));
        }

        return src;
    } catch (const std::runtime_error &e) {
        d->spirvCrossErrorMsg = QString::fromUtf8(e.what());
        return QByteArray();
    }
}

QByteArray QSpirvShader::translateToHLSL(int version) const
{
    d->spirvCrossErrorMsg.clear();

    try {
        if (!d->hlslGen)
            d->hlslGen = new spirv_cross::CompilerHLSL(reinterpret_cast<const uint32_t *>(d->ir.constData()), d->ir.size() / 4);

        spirv_cross::CompilerHLSL::Options options;
        options.shader_model = version;
        options.point_size_compat = true;
        d->hlslGen->set_hlsl_options(options);

        const std::string hlsl = d->hlslGen->compile();

        return QByteArray::fromStdString(hlsl);
    } catch (const std::runtime_error &e) {
        d->spirvCrossErrorMsg = QString::fromUtf8(e.what());
        return QByteArray();
    }
}

QByteArray QSpirvShader::translateToMSL(int version) const
{
    d->spirvCrossErrorMsg.clear();

    try {
        if (!d->mslGen)
            d->mslGen = new spirv_cross::CompilerMSL(reinterpret_cast<const uint32_t *>(d->ir.constData()), d->ir.size() / 4);

        spirv_cross::CompilerMSL::Options options;
        options.msl_version = spirv_cross::CompilerMSL::Options::make_msl_version(version / 10, version % 10);
        // leave platform set to macOS, it won't matter in practice (hopefully)
        d->mslGen->set_msl_options(options);

        const std::string msl = d->mslGen->compile();

        return QByteArray::fromStdString(msl);
    } catch (const std::runtime_error &e) {
        d->spirvCrossErrorMsg = QString::fromUtf8(e.what());
        return QByteArray();
    }
}

QString QSpirvShader::translationErrorMessage() const
{
    return d->spirvCrossErrorMsg;
}

QT_END_NAMESPACE

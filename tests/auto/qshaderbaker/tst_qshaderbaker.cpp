/****************************************************************************
**
** Copyright (C) 2019 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the test suite of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QtTest/QtTest>
#include <QFile>
#include <QtShaderTools/QShaderBaker>
#include <QtGui/QRhiShaderDescription>
#include <QtGui/QRhiShader>

class tst_QShaderBaker : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void emptyCompile();
    void noFileCompile();
    void noTargetsCompile();
    void noVariantsCompile();
    void simpleCompile();
    void simpleCompileNoSpirvSpecified();
    void simpleCompileCheckResults();
    void simpleCompileFromDevice();
    void simpleCompileFromString();
    void multiCompile();
    void reuse();
    void compileError();
    void translateError();
    void genVariants();
    void defines();
};

void tst_QShaderBaker::initTestCase()
{
}

void tst_QShaderBaker::cleanup()
{
}

void tst_QShaderBaker::emptyCompile()
{
    QShaderBaker baker;
    QRhiShader s = baker.bake();
    QVERIFY(!s.isValid());
    QVERIFY(!baker.errorMessage().isEmpty());
    qDebug() << baker.errorMessage();
}

void tst_QShaderBaker::noFileCompile()
{
    QShaderBaker baker;
    baker.setSourceFileName(QLatin1String(":/data/nonexistant.vert"));
    QRhiShader s = baker.bake();
    QVERIFY(!s.isValid());
    QVERIFY(!baker.errorMessage().isEmpty());
    qDebug() << baker.errorMessage();
}

void tst_QShaderBaker::noTargetsCompile()
{
    QShaderBaker baker;
    baker.setSourceFileName(QLatin1String(":/data/color.vert"));
    QRhiShader s = baker.bake();
    // an empty shader pack is invalid
    QVERIFY(!s.isValid());
    // not an error from the baker's point of view however
    QVERIFY(baker.errorMessage().isEmpty());
}

void tst_QShaderBaker::noVariantsCompile()
{
    QShaderBaker baker;
    baker.setSourceFileName(QLatin1String(":/data/color.vert"));
    QVector<QShaderBaker::GeneratedShader> targets;
    targets.append({ QRhiShaderKey::SpirvShader, QRhiShaderVersion(100) });
    baker.setGeneratedShaders(targets);
    QRhiShader s = baker.bake();
    // an empty shader pack is invalid
    QVERIFY(!s.isValid());
    // not an error from the baker's point of view however
    QVERIFY(baker.errorMessage().isEmpty());
}

void tst_QShaderBaker::simpleCompile()
{
    QShaderBaker baker;
    baker.setSourceFileName(QLatin1String(":/data/color.vert"));
    baker.setGeneratedShaderVariants({ QRhiShaderKey::StandardShader });
    QVector<QShaderBaker::GeneratedShader> targets;
    targets.append({ QRhiShaderKey::SpirvShader, QRhiShaderVersion(100) });
    baker.setGeneratedShaders(targets);
    QRhiShader s = baker.bake();
    QVERIFY(s.isValid());
    QVERIFY(baker.errorMessage().isEmpty());
    QCOMPARE(s.availableShaders().count(), 1);
    QVERIFY(s.availableShaders().contains(QRhiShaderKey(QRhiShaderKey::SpirvShader, QRhiShaderVersion(100))));
}

void tst_QShaderBaker::simpleCompileNoSpirvSpecified()
{
    QShaderBaker baker;
    baker.setSourceFileName(QLatin1String(":/data/color.vert"));
    baker.setGeneratedShaderVariants({ QRhiShaderKey::StandardShader });
    QVector<QShaderBaker::GeneratedShader> targets;
    targets.append({ QRhiShaderKey::GlslShader, QRhiShaderVersion(330) });
    baker.setGeneratedShaders(targets);
    QRhiShader s = baker.bake();
    QVERIFY(s.isValid());
    QVERIFY(baker.errorMessage().isEmpty());
    QCOMPARE(s.availableShaders().count(), 1);
    QVERIFY(s.availableShaders().contains(QRhiShaderKey(QRhiShaderKey::GlslShader, QRhiShaderVersion(330))));
    QVERIFY(s.shader(s.availableShaders().first()).shader().contains(QByteArrayLiteral("#version 330")));
}

void tst_QShaderBaker::simpleCompileCheckResults()
{
    QShaderBaker baker;
    baker.setSourceFileName(QLatin1String(":/data/color.vert"));
    baker.setGeneratedShaderVariants({ QRhiShaderKey::StandardShader });
    QVector<QShaderBaker::GeneratedShader> targets;
    targets.append({ QRhiShaderKey::SpirvShader, QRhiShaderVersion(100) });
    baker.setGeneratedShaders(targets);
    QRhiShader s = baker.bake();
    QVERIFY(s.isValid());
    QVERIFY(baker.errorMessage().isEmpty());
    QCOMPARE(s.availableShaders().count(), 1);

    const QRhiShaderCode shader = s.shader(QRhiShaderKey(QRhiShaderKey::SpirvShader,
                                                             QRhiShaderVersion(100)));
    QVERIFY(!shader.shader().isEmpty());
    QCOMPARE(shader.entryPoint(), QByteArrayLiteral("main"));

    const QRhiShaderDescription desc = s.description();
    QVERIFY(desc.isValid());
    QCOMPARE(desc.inputVariables().count(), 2);
    for (const QRhiShaderDescription::InOutVariable &v : desc.inputVariables()) {
        switch (v.location) {
        case 0:
            QCOMPARE(v.name, QLatin1String("position"));
            QCOMPARE(v.type, QRhiShaderDescription::Vec4);
            break;
        case 1:
            QCOMPARE(v.name, QLatin1String("color"));
            QCOMPARE(v.type, QRhiShaderDescription::Vec3);
            break;
        default:
            QVERIFY(false);
            break;
        }
    }
    QCOMPARE(desc.outputVariables().count(), 1);
    for (const QRhiShaderDescription::InOutVariable &v : desc.outputVariables()) {
        switch (v.location) {
        case 0:
            QCOMPARE(v.name, QLatin1String("v_color"));
            QCOMPARE(v.type, QRhiShaderDescription::Vec3);
            break;
        default:
            QVERIFY(false);
            break;
        }
    }
    QCOMPARE(desc.uniformBlocks().count(), 1);
    const QRhiShaderDescription::UniformBlock blk = desc.uniformBlocks().first();
    QCOMPARE(blk.blockName, QLatin1String("buf"));
    QCOMPARE(blk.structName, QLatin1String("ubuf"));
    QCOMPARE(blk.size, 68);
    QCOMPARE(blk.binding, 0);
    QCOMPARE(blk.descriptorSet, 0);
    QCOMPARE(blk.members.count(), 2);
    for (int i = 0; i < blk.members.count(); ++i) {
        const QRhiShaderDescription::BlockVariable v = blk.members[i];
        switch (i) {
        case 0:
            QCOMPARE(v.offset, 0);
            QCOMPARE(v.size, 64);
            QCOMPARE(v.name, QLatin1String("mvp"));
            QCOMPARE(v.type, QRhiShaderDescription::Mat4);
            QCOMPARE(v.matrixStride, 16);
            break;
        case 1:
            QCOMPARE(v.offset, 64);
            QCOMPARE(v.size, 4);
            QCOMPARE(v.name, QLatin1String("opacity"));
            QCOMPARE(v.type, QRhiShaderDescription::Float);
            break;
        default:
            QVERIFY(false);
            break;
        }
    }
}

void tst_QShaderBaker::simpleCompileFromDevice()
{
    QFile f(QLatin1String(":/data/color.vert"));
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));

    QShaderBaker baker;
    baker.setSourceDevice(&f, QRhiShader::VertexStage);
    baker.setGeneratedShaderVariants({ QRhiShaderKey::StandardShader });
    QVector<QShaderBaker::GeneratedShader> targets;
    targets.append({ QRhiShaderKey::SpirvShader, QRhiShaderVersion(100) });
    baker.setGeneratedShaders(targets);
    QRhiShader s = baker.bake();
    QVERIFY(s.isValid());
    QVERIFY(baker.errorMessage().isEmpty());
    QCOMPARE(s.availableShaders().count(), 1);
}

void tst_QShaderBaker::simpleCompileFromString()
{
    QFile f(QLatin1String(":/data/color.vert"));
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray contents = f.readAll();
    f.close();
    QVERIFY(!contents.isEmpty());

    QShaderBaker baker;
    baker.setSourceString(contents, QRhiShader::VertexStage);
    baker.setGeneratedShaderVariants({ QRhiShaderKey::StandardShader });
    QVector<QShaderBaker::GeneratedShader> targets;
    targets.append({ QRhiShaderKey::SpirvShader, QRhiShaderVersion(100) });
    baker.setGeneratedShaders(targets);
    QRhiShader s = baker.bake();
    QVERIFY(s.isValid());
    QVERIFY(baker.errorMessage().isEmpty());
    QCOMPARE(s.availableShaders().count(), 1);
}

void tst_QShaderBaker::multiCompile()
{
    QShaderBaker baker;
    baker.setSourceFileName(QLatin1String(":/data/color.vert"));
    baker.setGeneratedShaderVariants({ QRhiShaderKey::StandardShader });
    QVector<QShaderBaker::GeneratedShader> targets;
    targets.append({ QRhiShaderKey::SpirvShader, QRhiShaderVersion(100) });
    targets.append({ QRhiShaderKey::GlslShader, QRhiShaderVersion(100, QRhiShaderVersion::GlslEs) });
    targets.append({ QRhiShaderKey::GlslShader, QRhiShaderVersion(120) });
    targets.append({ QRhiShaderKey::HlslShader, QRhiShaderVersion(50) });
    targets.append({ QRhiShaderKey::MslShader, QRhiShaderVersion(12) });
    baker.setGeneratedShaders(targets);
    QRhiShader s = baker.bake();
    QVERIFY(s.isValid());
    QVERIFY(baker.errorMessage().isEmpty());
    QCOMPARE(s.availableShaders().count(), 5);

    for (const QShaderBaker::GeneratedShader &genShader : targets) {
        const QRhiShaderKey key(genShader.first, genShader.second);
        const QRhiShaderCode shader = s.shader(key);
        QVERIFY(!shader.shader().isEmpty());
        if (genShader.first != QRhiShaderKey::MslShader)
            QCOMPARE(shader.entryPoint(), QByteArrayLiteral("main"));
    }
}

void tst_QShaderBaker::reuse()
{
    QShaderBaker baker;
    baker.setSourceFileName(QLatin1String(":/data/color.vert"));
    baker.setGeneratedShaderVariants({ QRhiShaderKey::StandardShader });
    QVector<QShaderBaker::GeneratedShader> targets;
    targets.append({ QRhiShaderKey::SpirvShader, QRhiShaderVersion(100) });
    baker.setGeneratedShaders(targets);
    QRhiShader s = baker.bake();
    QVERIFY(s.isValid());
    QVERIFY(baker.errorMessage().isEmpty());
    QCOMPARE(s.availableShaders().count(), 1);

    baker.setSourceFileName(QLatin1String(":/data/color.frag"));
    targets.clear();
    targets.append({ QRhiShaderKey::SpirvShader, QRhiShaderVersion(100) });
    targets.append({ QRhiShaderKey::GlslShader, QRhiShaderVersion(100, QRhiShaderVersion::GlslEs) });
    targets.append({ QRhiShaderKey::GlslShader, QRhiShaderVersion(120) });
    targets.append({ QRhiShaderKey::HlslShader, QRhiShaderVersion(50) });
    targets.append({ QRhiShaderKey::MslShader, QRhiShaderVersion(12) });
    baker.setGeneratedShaders(targets);
    s = baker.bake();
    QVERIFY(s.isValid());
    QVERIFY(baker.errorMessage().isEmpty());
    QCOMPARE(s.availableShaders().count(), 5);
}

void tst_QShaderBaker::compileError()
{
    QShaderBaker baker;
    baker.setSourceFileName(QLatin1String(":/data/error.vert"));
    baker.setGeneratedShaderVariants({ QRhiShaderKey::StandardShader });
    QVector<QShaderBaker::GeneratedShader> targets;
    targets.append({ QRhiShaderKey::SpirvShader, QRhiShaderVersion(100) });
    baker.setGeneratedShaders(targets);
    QRhiShader s = baker.bake();
    QVERIFY(!s.isValid());
    QVERIFY(!baker.errorMessage().isEmpty());
    qDebug() << baker.errorMessage();
}

void tst_QShaderBaker::translateError()
{
    // assume the shader here fails in SPIRV-Cross with "cbuffer cannot be expressed with either HLSL packing layout or packoffset"
    QShaderBaker baker;
    baker.setSourceFileName(QLatin1String(":/data/hlsl_cbuf_error.frag"));
    baker.setGeneratedShaderVariants({ QRhiShaderKey::StandardShader });
    QVector<QShaderBaker::GeneratedShader> targets;
    targets.append({ QRhiShaderKey::HlslShader, QRhiShaderVersion(50) });
    baker.setGeneratedShaders(targets);
    QRhiShader s = baker.bake();
    QVERIFY(!s.isValid());
    QVERIFY(!baker.errorMessage().isEmpty());
    qDebug() << baker.errorMessage();
}

void tst_QShaderBaker::genVariants()
{
    QShaderBaker baker;
    baker.setSourceFileName(QLatin1String(":/data/color.vert"));
    baker.setGeneratedShaderVariants({
                                         QRhiShaderKey::StandardShader,
                                         QRhiShaderKey::BatchableVertexShader
                                     });
    QVector<QShaderBaker::GeneratedShader> targets;
    targets.append({ QRhiShaderKey::SpirvShader, QRhiShaderVersion(100) });
    targets.append({ QRhiShaderKey::GlslShader, QRhiShaderVersion(100, QRhiShaderVersion::GlslEs) });
    targets.append({ QRhiShaderKey::GlslShader, QRhiShaderVersion(330) });
    targets.append({ QRhiShaderKey::GlslShader, QRhiShaderVersion(120) });
    targets.append({ QRhiShaderKey::HlslShader, QRhiShaderVersion(50) });
    targets.append({ QRhiShaderKey::MslShader, QRhiShaderVersion(12) });
    baker.setGeneratedShaders(targets);
    QRhiShader s = baker.bake();
    QVERIFY(s.isValid());
    QVERIFY(baker.errorMessage().isEmpty());
    QCOMPARE(s.availableShaders().count(), 2 * 6);

    int batchableVariantCount = 0;
    int batchableGlslVariantCount = 0;
    for (const QRhiShaderKey &key : s.availableShaders()) {
        if (key.sourceVariant() == QRhiShaderKey::BatchableVertexShader) {
            ++batchableVariantCount;
            if (key.source() == QRhiShaderKey::GlslShader) {
                ++batchableGlslVariantCount;
                const QByteArray src = s.shader(key).shader();
                QVERIFY(src.contains(QByteArrayLiteral("_qt_order * ")));
            }
        }
    }
    QCOMPARE(batchableVariantCount, 6);
    QCOMPARE(batchableGlslVariantCount, 3);
}

void tst_QShaderBaker::defines()
{
    QShaderBaker baker;
    baker.setSourceFileName(QLatin1String(":/data/defines.frag"));
    baker.setGeneratedShaderVariants({ QRhiShaderKey::StandardShader });
    baker.setGeneratedShaders({ { QRhiShaderKey::SpirvShader, QRhiShaderVersion(100) } });
    QRhiShader s = baker.bake();
    QVERIFY(!s.isValid());
    QVERIFY(!baker.errorMessage().isEmpty());
    qDebug() << baker.errorMessage();

    QByteArray preamble;
    preamble = QByteArrayLiteral("#define DO_NOT_BREAK\n");
    baker.setPreamble(preamble);
    s = baker.bake();
    QVERIFY(s.isValid());
    QVERIFY(baker.errorMessage().isEmpty());

    QRhiShaderDescription desc = s.description();
    QCOMPARE(desc.uniformBlocks().count(), 1);
    QRhiShaderDescription::UniformBlock blk = desc.uniformBlocks().first();
    QCOMPARE(blk.members.count(), 2);
    bool opacity_ok = false;
    for (int i = 0; i < blk.members.count(); ++i) {
        const QRhiShaderDescription::BlockVariable v = blk.members[i];
        if (v.name == QLatin1String("opacity")) {
            opacity_ok = v.type == QRhiShaderDescription::Vec4;
            break;
        }
    }
    QVERIFY(opacity_ok);

    preamble += QByteArrayLiteral("#define OPACITY_SIZE 1\n");
    baker.setPreamble(preamble);
    s = baker.bake();
    QVERIFY(s.isValid());
    QVERIFY(baker.errorMessage().isEmpty());

    desc = s.description();
    blk = desc.uniformBlocks().first();
    opacity_ok = false;
    for (int i = 0; i < blk.members.count(); ++i) {
        const QRhiShaderDescription::BlockVariable v = blk.members[i];
        if (v.name == QLatin1String("opacity")) {
            opacity_ok = v.type == QRhiShaderDescription::Float;
            break;
        }
    }
    QVERIFY(opacity_ok);
}

#include <tst_qshaderbaker.moc>
QTEST_MAIN(tst_QShaderBaker)

/**
 * @file    test_display_pipeline.cpp
 * @brief   Regression tests for viewport geometry, palettes, and frame blending.
 * @author  cupidthecat
 * @date    2026-11-16
 *
 * Copyright (C) 2026 cupidthecat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#include "display_pipeline.h"

#include <QColor>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QSemaphore>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <future>

namespace {
QByteArray canonicalRgbaHash(const QImage &image) {
    QByteArray bytes;
    bytes.reserve(image.width() * image.height() * 4);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            assert(qAlpha(pixel) == 255);
            bytes.append(char(qRed(pixel)));
            bytes.append(char(qGreen(pixel)));
            bytes.append(char(qBlue(pixel)));
            bytes.append(char(qAlpha(pixel)));
        }
    }
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

std::array<QImage, 2> generatedFilterFrames() {
    const std::array<QRgb, 6> colors = {qRgb(21, 38, 59),   qRgb(227, 192, 114),
                                        qRgb(231, 69, 82),  qRgb(47, 154, 116),
                                        qRgb(69, 101, 165), qRgb(226, 230, 238)};
    const std::array<unsigned, 35> indices = {
        0, 0, 2, 1, 1, 3, 3, 0, 2, 2, 1, 4, 3, 5, 2, 2, 1, 1,
        4, 4, 5, 3, 3, 0, 5, 5, 4, 2, 3, 0, 0, 5, 0, 2, 2,
    };
    std::array<QImage, 2> frames = {QImage(7, 5, QImage::Format_RGB32),
                                    QImage(7, 5, QImage::Format_RGB32)};
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 7; ++x) {
            frames[0].setPixel(x, y, colors[indices[size_t(y * 7 + x)]]);
            const size_t shifted = size_t(((y + 1) % 5) * 7 + (x + 1) % 7);
            frames[1].setPixel(x, y, colors[(indices[shifted] + 1u) % colors.size()]);
        }
    }
    assert(canonicalRgbaHash(frames[0]) ==
           "e2300c709e09502f7086749a9201bc25dbb4e966729d5e2cbbb1329bbda284f6");
    assert(canonicalRgbaHash(frames[1]) ==
           "391bdf890051695612fb5acc2aa882cf2f427f3f2227d4da222820b0758d4f1c");
    return frames;
}

std::array<QImage, 2> generatedParallelEdgeFrames() {
    const std::array<QRgb, 8> colors = {
        qRgb(21, 38, 59),   qRgb(227, 192, 114), qRgb(231, 69, 82), qRgb(47, 154, 116),
        qRgb(69, 101, 165), qRgb(226, 230, 238), qRgb(1, 2, 3),     qRgb(253, 254, 255)};
    const std::array<unsigned, 35> cells = {0, 0, 2, 1, 1, 3, 3, 0, 2, 2, 1, 4, 3, 5, 2, 2, 1, 1,
                                            4, 4, 5, 3, 3, 0, 5, 5, 4, 2, 3, 0, 0, 5, 0, 2, 2};
    std::array<QImage, 2> frames = {QImage(160, 144, QImage::Format_RGB32),
                                    QImage(160, 144, QImage::Format_RGB32)};
    for (size_t phase = 0; phase < frames.size(); ++phase) {
        for (int y = 0; y < frames[phase].height(); ++y) {
            for (int x = 0; x < frames[phase].width(); ++x) {
                const size_t cell = size_t(((y + int(phase)) % 5) * 7 + (x + int(phase)) % 7);
                const size_t region = size_t(x / 21 + 3 * (y / 15));
                frames[phase].setPixel(x, y,
                                       colors[(cells[cell] + region + phase) % colors.size()]);
            }
        }
    }
    assert(canonicalRgbaHash(frames[0]) ==
           "07e3abb85011ee34f40ab522e67f1e66ba68a8dc5c279261cdbf7d9354a96b6d");
    assert(canonicalRgbaHash(frames[1]) ==
           "c872edab629b5d1b8a658b72fc70c45f98732003b4389f0c8426deaca6eb7c72");
    return frames;
}

void testParallelEdgeCaptures() {
    const std::array filters = {ScalingFilter::Scale2x, ScalingFilter::Scale4x,
                                ScalingFilter::AntialiasedScale2x,
                                ScalingFilter::AntialiasedScale4x};
    const std::array names = {"Scale2x", "Scale4x", "AntialiasedScale2x", "AntialiasedScale4x"};
    // The first two sizes straddle the 160,000-pixel parallel threshold.
    // Integer and uneven fractional scaling also cross repeated-row boundaries.
    const std::array sizes = {QSize(401, 399), QSize(400, 400), QSize(480, 432), QSize(523, 417)};
    // Captured from the serial a37a680 pipeline, using only the generated frames
    // above. Hashes encode top-down RGBA; phases are Disabled, Simple, Accurate
    // even, Accurate odd. Every pixel, including the outer rows, is opaque.
    const char *hashes[4][4][4] = {
        {
            // Scale2x
            {"e72ab047bab24f4120b28f020166d9f5c258b6676de33beeb26193f9cbf747cb",
             "c92df34581d1d2965e5b9f26de49ee85ef0003c7ff12cd5837fbff8eee86bee2",
             "cb044edf0deb70bc64f550f05c95afaab0fa2b2ae23926d087d7e2b1f16ec9af",
             "f29db6560a30262e4f278318b88a6d2725ac17e1434dd9d2d63355867670f2a4"},
            {"ccdc728a0d2f3fecd441fa50257541f67634e6fe126f5de053184943fa4ee1d4",
             "a6003a946c30416a6948e9f4c2f134cb5dae2c6b5e0e54b31a0dba37c51904dd",
             "ac58b21ea3894b79e792b9c77177d01c63cc70772038109da36b1cfd2063ff7f",
             "ecec6552bf78f6d176646413bdc986ad276429fa5dc353793b6a506bf73c0959"},
            {"4745da2e55bf06e632425dedcc16d25fa1c3e67975569af0bff5b258ec9cd024",
             "4acbfdf8427280b1d9d45633070f44e2df9006f3eba22fc740e09b94086550ee",
             "2df6668410e1e5e207334b1682c5c9644bf1418ec0f5d406010cfda9025d644f",
             "02e821f005f40b1896a39ed8674d4c151a18e3b028668134ec911e6824b8edf6"},
            {"d7a383a2ac07c9858e77fb95564cad32abb8fc40b0d74d862c684dad63e44e7d",
             "e1c4c3b6a843674f17eae212ab2bdd6d85bedce847a3275164a4f1ace9d91536",
             "f0ecb08de344c1421ffdebe24208b07eb2d8da15650aec7a3cca3972c15b1d16",
             "1231dc6f199b2079b68bdf50d8566710655598286b47a872a90ed1a65be32db7"},
        },
        {
            // Scale4x
            {"0f7562c4b441964db969e85e49d979330873f3489c189207186d2902183e361b",
             "461dc82d1b8d0843e5bb083a9f8cf9ba21270694f7eb0f3b34d5efa8c4916188",
             "f547c9c42330650592d92b5e13d219e1f5e17044e0773336d47dfe22cf189a6e",
             "cbce6df2acda087e616a1418e0680b1b23420fb9e943a7ef95abca1ef5a4bc69"},
            {"79b1fa50dcb1c7223b34d6bd32f03fbff4c1697e56f43e5a01bb5d1272c2bd4b",
             "9973ed22ebc8311cabb3222cc7c662cd3cef2924d7727188f82d5449d9e02841",
             "80814b362cf37922c8533f77813b6c9640d0eeb8fcbaf9eaf580d4f8b6f7a089",
             "964762539102630a6561deb8d2734f7ab0b75203755e3d7a985a21015ad4072d"},
            {"cbd5fb32dd603cf6cf77b4b427ca5378782ce4f944916dfa8c009bd054412a17",
             "b809cd97d22ba40e9f295cfc636c29eaba3109718c63e67f92b35e8a642befd8",
             "294992be7ef80321307b813c165f3a3a3d446f3308ef178b08641b8062c89c4c",
             "406606d7e8f30378c3bd1fe31a1735fa861552f1950289290e303aafb5101139"},
            {"fa3dbbd7c53f9cdef071c287780fc0cbabd2803339548100f73fea483b81de5a",
             "c30ca29af0f9810fccab70a5a97eca799c778008b784b26cfedfe113175c1fba",
             "19d10b7be9d4303e6afc58b1840c40b7244d60ba259cfa35e3de6d5b3bcbe059",
             "ce779e625603de1bc8e1374422f45075ada81225d9ac78e7e5f759d0d4e6ca7f"},
        },
        {
            // AntialiasedScale2x
            {"36bb7af7d29b957124c27ea69709805d1bae035049b5575d30b8a87ceb65660f",
             "443f878f4ad620295b27c2a213468d4c229c591a860cab6a7b2e1bf5ed7872e9",
             "d33e9fce61bd02081916008fa89d79816dda0e76fb88bbc5aa44895e337c068a",
             "36288ce920c463e5234a0907d97d0662d50752b761b087da620714f8f11ede4b"},
            {"a94b99eea6674bead1ea9b43d3a7453a3ba64637e4099efd79ec26101eed03d2",
             "a7c1f9ec072c7a89f553e62141d9e953f5e751626371a2ea1a6112d850e0d13f",
             "b3f6df66700fc8477302210943287dfee8db9b80b83094933b46d4986d836be0",
             "613d0466a7b1ad2e779cd87e5a2d74ab10b59e1883acab72fb0aa7d5538c7c18"},
            {"c93ea952a41515168b56e48d48ae3f61df9c5562a90aec3854cad6ca08de433e",
             "326f16966cc7a2e70f27c5d10b6f61602dd9d7c5a41e1cf28dd9004994521773",
             "b6804f58c9070d0ba7b263bb05f83c5d837dc8a0807058e2a356ac3ac3db88a8",
             "67d073bc76ac16a76043d9b705d99a9224a90ec1368144f7b6490a1e2a1bf98f"},
            {"db732fcd0ac07f5aa608ef37a016690f27b667630341b7d0518ac73e75aa4028",
             "abf9fa7cf3592b1d76505f142fc4b7d8a44710f9d54f38d74e7a8f68964f7bf5",
             "844549768394128a0771432503210410eca4f38385a7a6cf84a59f702cef5d26",
             "8e928c55bb66619b55efdbbde259f397d6d23433f246f9b586f624a686668e55"},
        },
        {
            // AntialiasedScale4x
            {"9dd1b9387d6fdf6d3e871b6e7a05d6e9b817202ed2f0e266cc20cf6e69ec3c0f",
             "cc576759396fc68cb31cc5ef89fed5412462df58fec35624b8eae388ec3772c2",
             "23fa3fb9449072e0e503455390a2c6b66a2391e9c958a2a7b2c45744b042a070",
             "7f325a9fda8e07cc97d5e558b137581955a89112b8668adbdff43298dd84bfdc"},
            {"3c660b9d96713d9f587fdfe7937f42965ae6bb451a905d03fa46cde9c5178558",
             "82b4f6edad44fbfb9969255a983ce18cd264f4713008cd20a7e3fb660767df7e",
             "25bccd2a94e33fb733ce66aced327b5a2ed24e55146c58e37c1ef03089408950",
             "d641569266b4b5d4234956aec1e62ff62b0f9fd15432811dd24f004f3d8dde25"},
            {"013564daaad1bf1191ccf183781ef8d3538bef862ab948aac74221781aa2c764",
             "6adb47eb1eb01cf40597cadfcfdc19b56a33b4c2d32da09235c2a982286c2b75",
             "a40a58f01559028ff5e4262f4af8dcba901bdf814f0ded98595a04dcd783fb86",
             "abc032963ae278209c1cc57fc3c2df2e49ebac7dfd0a2e72d9d8faa72e125bb7"},
            {"242cd764229494e017001ae75b32b6f4466609f81d9eb9a6c3a72df04c6cdc9f",
             "402139ea1ec7205e7a99567e5284e156b9c22bd55b25046249161a2dc6e3f1c2",
             "0a28e0fd800b200f978aecc19da09ada25cab8c9d901e3a074f1b73af7519207",
             "8ad11e4f2e980c05ab2102d02c1fad4823528254ba6daa4214950efe8006085d"},
        },
    };
    const auto frames = generatedParallelEdgeFrames();
    const auto renderHash = [&](size_t filter, size_t size, unsigned phase) {
        const auto mode = phase == 0   ? FrameBlending::Disabled
                          : phase == 1 ? FrameBlending::Simple
                                       : FrameBlending::Accurate;
        const auto output = renderDisplayFrames(frames[0], frames[1], sizes[size], filters[filter],
                                                mode, phase == 3);
        assert(output.size() == sizes[size]);
        return canonicalRgbaHash(output);
    };
    const auto check = [&](const QByteArray &observed, size_t filter, size_t size, unsigned phase) {
        if (observed != hashes[filter][size][phase]) {
            std::fprintf(stderr, "large %s %dx%d blend %u: %s, expected %s\n", names[filter],
                         sizes[size].width(), sizes[size].height(), phase, observed.constData(),
                         hashes[filter][size][phase]);
        }
        assert(observed == hashes[filter][size][phase]);
    };

    // Start more simultaneous callers than the dedicated worker capacity.
    // Hash each result as soon as its synchronous call returns. Run this before
    // the serial cases so shared tables and the worker pool also initialize
    // under contention; exhausted worker slots must permit inline progress.
    QSemaphore ready;
    QSemaphore start;
    std::array<std::future<QByteArray>, 8> pending;
    for (size_t job = 0; job < pending.size(); ++job) {
        pending[job] = std::async(std::launch::async, [&, job] {
            ready.release();
            start.acquire();
            return renderHash(job / 2, 2 + job % 2, unsigned(2 + job % 2));
        });
    }
    ready.acquire(int(pending.size()));
    start.release(int(pending.size()));
    for (size_t job = 0; job < pending.size(); ++job)
        check(pending[job].get(), job / 2, 2 + job % 2, unsigned(2 + job % 2));

    for (size_t filter = 0; filter < filters.size(); ++filter)
        for (size_t size = 0; size < sizes.size(); ++size)
            for (unsigned phase = 0; phase < 4; ++phase)
                check(renderHash(filter, size, phase), filter, size, phase);

    // Above 4x height, even Scale4x can copy repeated half-cell rows. With two
    // workers, the final band starts at row 480, in the same half-cell as row
    // 479. It must render its first row without reading another band's output.
    const char *repeatedRowHashes[2][2] = {
        {"99e1604aa346016ac803ed6beafe598d796506fa3ee260e32f7fd38af112af12",
         "1000c15e673c17a08311e235e5aeee71ee525eb1bf4359cf76371fff6ce040b7"},
        {"252c4c8df0d2cfa153581cd8a655140bc82086235da48a88b1fef4bd76a816ff",
         "bcd4a3e3c9ded8d19668f526ceac66c65e6c758884d17792c6e253b7354c2db3"},
    };
    for (size_t filter = 0; filter < 2; ++filter) {
        for (unsigned odd = 0; odd < 2; ++odd) {
            const auto output =
                renderDisplayFrames(frames[0], frames[1], {801, 721}, filters[1 + 2 * filter],
                                    FrameBlending::Accurate, odd != 0);
            assert(output.size() == QSize(801, 721));
            const auto observed = canonicalRgbaHash(output);
            if (observed != repeatedRowHashes[filter][odd]) {
                std::fprintf(stderr, "repeated-row %s 801x721 odd %u: %s, expected %s\n",
                             names[1 + 2 * filter], odd, observed.constData(),
                             repeatedRowHashes[filter][odd]);
            }
            assert(observed == repeatedRowHashes[filter][odd]);
        }
    }
    assert(canonicalRgbaHash(frames[0]) ==
           "07e3abb85011ee34f40ab522e67f1e66ba68a8dc5c279261cdbf7d9354a96b6d");
    assert(canonicalRgbaHash(frames[1]) ==
           "c872edab629b5d1b8a658b72fc70c45f98732003b4389f0c8426deaca6eb7c72");
}

void testExpandedFilterCaptures() {
    static_assert(int(ScalingFilter::Nearest) == 0 && int(ScalingFilter::Bilinear) == 1 &&
                  int(ScalingFilter::SmoothBilinear) == 2 && int(ScalingFilter::Pixelated) == 3 &&
                  int(ScalingFilter::Scale2x) == 4 && int(ScalingFilter::Scale4x) == 5 &&
                  int(ScalingFilter::AntialiasedScale2x) == 6 &&
                  int(ScalingFilter::AntialiasedScale4x) == 7 && int(ScalingFilter::Count) == 8);
    const std::array filters = {ScalingFilter::Pixelated, ScalingFilter::Scale2x,
                                ScalingFilter::Scale4x, ScalingFilter::AntialiasedScale2x,
                                ScalingFilter::AntialiasedScale4x};
    const std::array names = {"Pixelated", "Scale2x", "Scale4x", "AntialiasedScale2x",
                              "AntialiasedScale4x"};
    const std::array sizes = {QSize(5, 3),   QSize(7, 5),   QSize(9, 7),
                              QSize(14, 10), QSize(17, 13), QSize(28, 20)};
    // Canonical top-down RGBA captures of the generated controls above. Each
    // row is Disabled, Simple, Accurate-even, Accurate-odd; unity and downscaling
    // are included because edge filters need not preserve their native samples.
    const char *hashes[5][6][4] = {
        {
            // Pixelated
            {"69fecf9c7f739118a62f06519dba3411883981e01a7a69eefca70d67419a0605",
             "002e66474805eecaff30b5743f1faca45698721b6509b5bed62289f793023d30",
             "3cdd470b6b2c5d7e3c2db57b932179e80fd8942ed6b98a4c60bdcf87f3d461e4",
             "79fb95814920910c3c31f0fe414ead251197cb7a61f7ea703c60944bdcb7bf99"},
            {"e2300c709e09502f7086749a9201bc25dbb4e966729d5e2cbbb1329bbda284f6",
             "e47d0887d13549876d027faab1beab10933ada3145cf2a03b0f0d4a9224e20c5",
             "95c471bf5fa7c154cc71ad7c6853112032324620d60a4543302f9c572db41433",
             "be9d7189c1a7a2324be975878dc22685e3289ed124457b032511a8944724a2a8"},
            {"840e37b69b7d30aa618af3e59dc5be6c27f4b6ad543e2d90fa813e0c35fc15a6",
             "da4e16fe0ae0f59fc5e60b3008a9d25640928cb820e4dbe605bc74a65537e609",
             "eea010418598f8b7767fd09e77b8538f3972028deb0b89f913f8511c48898907",
             "fc563436e64a4175746b872835365ca7e15d3e3de655eeaf24654304cafd12d0"},
            {"1c256b3f78227155fd48083ea162993d4e761ef547da7ed4d3fc8f4e54710254",
             "378ddd1be5dec0a180c2bb20075bfa516dcf560ac6f6a3b4816a7cf63d2afeec",
             "6198d28fc10c1968e32377771f464bf6d088e4113376d55871d922c872165089",
             "0b85024e6e39631fb02dff6902b7d65bfd2752c69c1a22a1c25eccff9cb6e9f0"},
            {"6e9f80ec703abc2cdb822ee631e65df0e4bef891643e15762031834bf3f00853",
             "e42171d07b72d5a2ccca557b5674a93623fef64503798b22783f8a9edf05ff2a",
             "a79f6b685e8244d91b781e15a209f26e4aedfe1218b8e5ce9e0cc4b808a2f2b2",
             "549fd05076d8df9b52e689594e2e7df941e0b9de0d18a3ffac80efada30e306b"},
            {"f345ec6acc9f8bc25aa24dc4fba6d0181792ac00ecafa47e304407c4a7368aec",
             "d34a4d258601288b2f7b5285fc6ca72f2ce1e8ad26176eeae3bf3fb0fae6b035",
             "c2e4277455e068892ff5c26166b1f2c8a5842830257b713c5d62bc5a81a4bee7",
             "9bf30a15836d92c833470497c859331fbf522dabbd4b2597ae0c71280a900979"},
        },
        {
            // Scale2x
            {"facbebc02a4d63eaacf8b77064d952744100a9b86a3bac98420ef91de5a6b5cb",
             "597ee643334a20e1b3df58bd16a1bcc8f20c38973bf7b103bb3792d0b86a82d6",
             "d8a96ce55cef5d596a5b6efc8d9d6c278473aadba6dfb382aa35bcac30584b3e",
             "6e5c33fbaa213b7178e3ed146563fd5517535567f0dfcb13b99965773ea3ea6e"},
            {"e6336c84f3f1e4f045e6a98691d037b592a9b9064408de47fed53e1e1c9b32d8",
             "3136cc05feb21d2a98a86bf26473cd475948136ad27d795e1ee2f08bcd1fd220",
             "6fc8455234fec586d6795eeb82dc6b95b2f8aef501cc6b0a6b40f57f2d84a579",
             "8d8172e870f7214ccea7aa285b096857995f79822d7e4d01b4b3f2cea163a793"},
            {"742a1039297abf7d7f486a5a16667760b04c8c31485a2e55f314019287403da9",
             "7a6850ef5353183d51101ca461d3310d2318050d281f67b1753be43611c56c11",
             "26ad3b0b69b38512afa7820c78f44f6ce1ec7422a50d0a9ccd6a5643932384c6",
             "883e8c13b82f6ba3f847ad7acb8f6f8eb7f65756edbb642c753ab1180442e075"},
            {"aab235b1a0500fb6cc547c36cee34c7161c5103700f33cedbd981b952d0a843b",
             "806e4f6bf41f0941f6b6bab75318e422af1f45fce98f923c49f90f75b3baf306",
             "f89023e485b71f77dfdd265206bcf3f09633353107e683f372fbd31e768a5557",
             "2c6b3361dbfb024ebdeadbbac7d40b53095cb568a59f69b91c076bd195a18b53"},
            {"01260b857362773d288cc9b9e8b16d78dfd3ca9cf777b1b49a164d700d5200d5",
             "10fa50ea7173949a189c737d47b7f2ca02d33ae4965c31f8b198acb612e196be",
             "10f4226b5e4d729149accc15bcda4d5d6c9e3161b95f76dda8134df8df789d64",
             "7893408a2d2600c8b8441e1573ea7e0bf2663967b0784422f16e42cdf37af938"},
            {"009340d2aafba138a407b7a3451b7870e81dd83e5fe76d90a9a8f8c8b325aea4",
             "f82eeccdb3f1593934f185eb00893118830b9159c55c6a1da1518740097fdbb4",
             "46305e900f431e32ea2bd0632213b924984ccf1cff84fa1b774485258b4cd3ea",
             "e77b0f28e8e67e83773ea275a2f4e9096f218bb53c9fa13d9ac95ec9d516327e"},
        },
        {
            // Scale4x
            {"facbebc02a4d63eaacf8b77064d952744100a9b86a3bac98420ef91de5a6b5cb",
             "6d45e0bb756051a04d54c229de6a039f55161907861ccbe436a8dea01610cd6f",
             "52d99d52ee8eef5cab78dbe6f590b8bbda59183ce9e820b38e335ee3affb8fd5",
             "878d9818e561ea2dc368a3abc0a80acfbd1253cda01ac83997c5d8a0df6ee4a7"},
            {"e2300c709e09502f7086749a9201bc25dbb4e966729d5e2cbbb1329bbda284f6",
             "e47d0887d13549876d027faab1beab10933ada3145cf2a03b0f0d4a9224e20c5",
             "95c471bf5fa7c154cc71ad7c6853112032324620d60a4543302f9c572db41433",
             "be9d7189c1a7a2324be975878dc22685e3289ed124457b032511a8944724a2a8"},
            {"a3657b8def6ba48793b0e2afbce414e5160b98a2175a31abca21799990fe30f6",
             "207fadb39e13b9c728f0a15ab9506cd0d831e59cafab3b0186d02834990485ec",
             "cf45f7a860e70094c49c9b3b9a2b5fd795e83ae6c2c8afb34e63bee632eeb421",
             "af8d15517149d285e691e6c7a05eefa464ba7fdf25d6d1bb45373b9e64e4910c"},
            {"9e1e737aba5c1a7392d5e902a6630ba7e82982f75fe563de325b3c080974aa88",
             "0dfa8d453daee8e621e278522a4c1fb4ea7b01388c30013be6666f963fd3674d",
             "091cb4a7e7cd2288b9634e4e8b21d179d74d6310a37a854f1d0c8132729c5dd7",
             "7d63e2e0045e3c3cbc1142c705e7f8d107376db05130d1f9d967f8bfdd40afbc"},
            {"bfa1ac5c27a1e2ae718f666ec4f08a2710bd8f9bdd7fb75ea07fb10f1f545ba1",
             "0c2a4e99c659421cb8e4c766b68d1b97ec9e42d0cb912402845e99956d1b0aed",
             "d5fc301a957b3d7785f9528a428f0356f7deff580ffffd333cf0e3a77629392b",
             "83933675a4b45bad3b07c036f3d0c3b55185721f43665f2e993050656dbf1bb4"},
            {"ba2b1734795f8d862f7a18033902b8a3844e648817242876989276a98c812ab7",
             "a04235d84d8457ca4e49c42e1ebfc9dddcf7d9973d4a195dbcb05d6c8c1b219e",
             "d79c5a0ed35399743cc4883140be564a6ce1a4fc3804a6944758299c9622cc00",
             "65db43517898930d4707158fbe2ccd60dd7e35548bc3e43d7c3b23bc59266428"},
        },
        {
            // AAScale2x
            {"10cc7d8486521fa9bcc43d6cc2f12704e2c1dbb4966954346411d6dd7339c824",
             "980cb8161606a1431ad08063d70fcb7fe3ec8ff310b2c5d104e4184d8ed32616",
             "79a36e598637c9f164fbe598611a53bbb1acc00ded8dfb2f1e8f2e5c4d1e413f",
             "b06fcf6aefdb58d5c076d8022d0d6e0fdb57338bfcc785a722a7a53633df0240"},
            {"6022aa85e090b12ea000555f4aba59eebcf351b4257ae04c7434d62d1f52f96f",
             "4601039e874e3ff3393549631cfb5e4562481fe3fc37c4be42df17ccc5b852a4",
             "6dc8c183f4601f7ad07c7c94eafd5d1ee4e5e2b949137215eab73fd3c3bb6bab",
             "c773162dce3574e655a844307fc4980643a7e4368c40b3ee8faec155f02adb26"},
            {"aae0a0795cbc7646e28085c55c15a7a18867437d8ad4a82a8a334f783d674acb",
             "428e5b9455ea7ebb3a372245376d2c14b9c4475724bc6d6e9e7e5b8503d6eb5d",
             "2b998d01b7b5aa6d399c1ab9439460a5503f84652d45f3fc704c51ede12762ef",
             "b5d32908f340c67a728a2fa528543abf9e814a0ff768135525638df6d474fbae"},
            {"054a0bbbe2e62ab900b759c099f45a98cf1c9b4736a65a0aef205c2eed0ff7ee",
             "446075fadc94c0b5794fe1130ba1ff7bd3ec05cdc5812c175c58d3865edc758e",
             "8fd7540cab605fabe3f841dff72657370e0398e3b02fe739700e171f26add57f",
             "7c52688fbda5bf251d3b92985f04a4c7dee0f69080ccda3ffe45c1a2c9f4e36e"},
            {"8330d14730fe5487cfce9b9050eee45bd04373d229833eb5eb6a6a162d5a31db",
             "64c24e2137a2ec09746ed744a192158ea2952d97a94a91330a9451e5a1cbf05e",
             "ff424baabb95895120dd589bd9595829c20244390488a7741440c771f0c67ca3",
             "b0921e261a2e739c0a43e754a70d5250ad162feeb192b0fc0e486a2a18d25fbb"},
            {"99accf5b815d89969b2c63a5e7c01b5f709a31694950d01a9879ce56cdcc5274",
             "fce60aa4b707408bffc640dc3b67974e79c986227082342f5ecbce511051e2ea",
             "df7fd80f88a715efbecff7faaf9953a9ac6ef409e089323a99b4ce19f21180cd",
             "790c903ba180d2bf0e8eade97e2e17fb1de1af76b0738ab5390e0fa7fa887c55"},
        },
        {
            // AAScale4x
            {"10cc7d8486521fa9bcc43d6cc2f12704e2c1dbb4966954346411d6dd7339c824",
             "392930c644fabde6cdd6a67c0a86eb2d168b8e635055a416f5acc00113f75267",
             "f21f36e2b56cfac2ad08158e2446b3a73ba135e3a7b187efe8169d82fae603ca",
             "0c4c8de7ee57a3c92f138eb0ce9b4b65a131be6cc38f4eaaafd1bd55e1eee4cc"},
            {"58b1b7ab5bf6f7e479d930a225cf59efaaeaf274bda0d78eb643103321d9fedb",
             "dd9feaf691e8ae6f9a534b7b944ab34dd154d234b9191c7a59aa98a36f9c15cc",
             "90869939f9bfd55d03d889dafbfb6be931612cb8d8540fdba09738a0ff3c37e6",
             "6aafc7d99d8e1aea76fc665d7de67c9465b84b73a0dee81d40ca64d0725fab03"},
            {"2cdb13a2263989d433cb5a2860abcca4b58392f0d5def94990c61bf92bb4bb97",
             "fbd7c56ed4db8ce1895133c1209c3b9025bd08eccb5baf311c75768f587c44c7",
             "6891830c18ca499ee3aa7e9d7e2e158ae7c6e19e2250bbfa6c53bc7dd4b3c4ef",
             "6cc3ba52033b0df2161b7ad73869bf1630e3541bcead972fd5493c05ecf74747"},
            {"726a10821b55e0590f4bb213ddd0ec9541566baef62ce85f71ba9967a1f4b92b",
             "9807c4d71cba6356fd183386cafd465f810f4395f34fe31e43676a5883cfc85a",
             "1ad30bba34cb1eb43d96e513226e3d0ae0f58722605ee8da5e4af69e22f9989f",
             "6db021eb02a486cc8635dc8d10268e4190cd3076deb67b2d6b940e4b160fde6f"},
            {"1938e39145e35c55e7a2946c6d1f828542af42d6b484534cc4956583cb1aeb73",
             "681cbb385313fb06e8310e0a753e4f47ef7f173d056d792a83c9f4c9ab1c5071",
             "323576cd4f4bb64563bc550f2fea880c1045e63fdf73451fec36343ae0329e85",
             "c6cd55fa2be394bab8176b85afbc1d34b89792a51afa0600fe392de30d3a5026"},
            {"f5fefbc7f90042efc3ad7b9d2dbb53716a3ea60fa72a6ce3701a5652a218f283",
             "c4510361cc63859bccaa9b64851c918ac21faabedfbfe8f7bdec817a142aaef1",
             "ab1d4e85d02b0acbfec5e265454c65caa74c662353ee616067d2fa3531ec2760",
             "902e3e613effcf8432c2f972bec155bb637ffb111fa95017a69effa4794b1c43"},
        },
    };
    const auto frames = generatedFilterFrames();
    for (size_t filter = 0; filter < filters.size(); ++filter) {
        assert(filterDisplayImage({}, sizes[0], filters[filter]).isNull());
        assert(filterDisplayImage(frames[0], {}, filters[filter]).isNull());
        for (size_t size = 0; size < sizes.size(); ++size) {
            for (unsigned phase = 0; phase < 4; ++phase) {
                const auto mode = phase == 0   ? FrameBlending::Disabled
                                  : phase == 1 ? FrameBlending::Simple
                                               : FrameBlending::Accurate;
                const auto output = renderDisplayFrames(frames[0], frames[1], sizes[size],
                                                        filters[filter], mode, phase == 3);
                assert(output.size() == sizes[size]);
                const auto observed = canonicalRgbaHash(output);
                if (observed != hashes[filter][size][phase]) {
                    std::fprintf(stderr, "%s %dx%d blend %u: %s, expected %s\n", names[filter],
                                 sizes[size].width(), sizes[size].height(), phase,
                                 observed.constData(), hashes[filter][size][phase]);
                }
                assert(observed == hashes[filter][size][phase]);
            }
            const auto filtered = filterDisplayImage(frames[0], sizes[size], filters[filter]);
            assert(canonicalRgbaHash(filtered) == hashes[filter][size][0]);
            assert(renderDisplayFrames(frames[0], {}, sizes[size], filters[filter],
                                       FrameBlending::Accurate, true) == filtered);
            assert(renderDisplayFrames(frames[0], QImage(1, 1, QImage::Format_RGB32), sizes[size],
                                       filters[filter], FrameBlending::Simple, false) == filtered);
        }
    }
}

void testExpandedFilterOrientation() {
    QImage stripes(7, 5, QImage::Format_RGB32);
    const std::array<QRgb, 5> colors = {qRgb(255, 0, 0), qRgb(255, 127, 0), qRgb(0, 255, 0),
                                        qRgb(0, 127, 255), qRgb(0, 0, 255)};
    for (int y = 0; y < stripes.height(); ++y)
        for (int x = 0; x < stripes.width(); ++x)
            stripes.setPixel(x, y, colors[size_t(y)]);
    for (const auto filter :
         {ScalingFilter::Pixelated, ScalingFilter::Scale2x, ScalingFilter::Scale4x,
          ScalingFilter::AntialiasedScale2x, ScalingFilter::AntialiasedScale4x}) {
        for (const auto size : {QSize(5, 3), QSize(17, 13)}) {
            const auto output = filterDisplayImage(stripes, size, filter);
            for (int y = 0; y < output.height(); ++y)
                for (int x = 0; x < output.width(); ++x) {
                    const QRgb pixel = output.pixel(x, y);
                    assert(qAlpha(pixel) == 255 && pixel == output.pixel(0, y));
                }
            assert(qRed(output.pixel(0, 0)) > qBlue(output.pixel(0, 0)));
            assert(qBlue(output.pixel(0, output.height() - 1)) >
                   qRed(output.pixel(0, output.height() - 1)));
        }
    }
}

void testPreciseNearestSampling() {
    QImage source(160, 144, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixel(x, y, qRgb(x, y, 31));
    for (auto size :
         {QSize(320, 288), QSize(400, 360), QSize(401, 359), QSize(200, 180), QSize(79, 71)}) {
        const auto output = filterDisplayImage(source, size, ScalingFilter::Nearest);
        for (int y = 0; y < size.height(); ++y)
            for (int x = 0; x < size.width(); ++x) {
                const int sx = int(std::floor((static_cast<long double>(x) + 0.5L) *
                                              source.width() / size.width()));
                const int sy = int(std::floor((static_cast<long double>(y) + 0.5L) *
                                              source.height() / size.height()));
                assert(output.pixel(x, y) == source.pixel(sx, sy));
            }
    }
    assert(scaleNearestDisplayImage({}, {4, 4}).isNull());
    assert(scaleNearestDisplayImage(source, {}).isNull());
    assert(scaleNearestDisplayImage(source, source.size()) == source);

    QImage translucent(2, 2, QImage::Format_ARGB32_Premultiplied);
    translucent.fill(QColor(170, 84, 33, 129));
    translucent.setColorSpace(QColorSpace::DisplayP3);
    translucent.setDevicePixelRatio(1.25);
    const auto painted = filterDisplayImage(translucent, {5, 3}, ScalingFilter::Nearest);
    assert(painted.format() == translucent.format());
    assert(painted.pixel(2, 1) == translucent.pixel(1, 1));
    assert(painted.colorSpace() == translucent.colorSpace());
    assert(painted.devicePixelRatio() == translucent.devicePixelRatio());
}

void testFilteredFrameBlending() {
    QImage white(160, 144, QImage::Format_RGB32);
    QImage black(160, 144, QImage::Format_RGB32);
    white.fill(Qt::white);
    black.fill(Qt::black);
    for (auto filter : {ScalingFilter::Bilinear, ScalingFilter::SmoothBilinear}) {
        for (auto size : {QSize(320, 288), QSize(401, 359), QSize(79, 71)}) {
            for (bool odd : {false, true}) {
                const auto output =
                    renderDisplayFrames(white, black, size, filter, FrameBlending::Accurate, odd);
                for (int y = 0; y < size.height(); ++y) {
                    const int sourceRow = int((y + 0.5) * white.height() / size.height());
                    const double oldWeight = ((sourceRow & 1) != int(odd)) ? 2.0 / 3.0 : 1.0 / 3.0;
                    const int expected =
                        int(std::lround(255 * std::pow(1.0 - oldWeight, 1.0 / 2.2)));
                    assert(std::abs(qRed(output.pixel(size.width() / 2, y)) - expected) <= 1);
                }
            }
            const auto simple =
                renderDisplayFrames(white, black, size, filter, FrameBlending::Simple, false);
            assert(simple.pixel(size.width() / 2, size.height() / 2) == qRgb(186, 186, 186));
            assert(renderDisplayFrames(white, black, size, filter, FrameBlending::Disabled,
                                       false) == filterDisplayImage(white, size, filter));
        }
    }
    QImage current(3, 4, QImage::Format_RGB32);
    QImage previous(3, 4, QImage::Format_RGB32);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 3; ++x) {
            current.setPixel(x, y, qRgb(12 + x * 70, 30 + y * 50, 230 - x * 60 - y * 10));
            previous.setPixel(x, y, qRgb(220 - y * 40, 20 + x * 80, 20 + y * 45));
        }
    for (auto size : {QSize(6, 8), QSize(7, 11), QSize(2, 2), current.size()}) {
        for (auto mode :
             {FrameBlending::Disabled, FrameBlending::Simple, FrameBlending::Accurate}) {
            for (bool odd : {false, true}) {
                assert(renderDisplayFrames(current, previous, size, ScalingFilter::Nearest, mode,
                                           odd) ==
                       scaleNearestDisplayImage(blendDisplayFrames(current, previous, mode, odd),
                                                size));
                for (auto filter : {ScalingFilter::Bilinear, ScalingFilter::SmoothBilinear}) {
                    const auto output =
                        renderDisplayFrames(current, previous, size, filter, mode, odd);
                    for (int y = 0; y < size.height(); ++y)
                        for (int x = 0; x < size.width(); ++x) {
                            const double px = (x + 0.5) * current.width() / size.width() - 0.5;
                            const double py = (y + 0.5) * current.height() / size.height() - 0.5;
                            const int left = int(std::floor(px)), top = int(std::floor(py));
                            double wx = px - left, wy = py - top;
                            if (filter == ScalingFilter::SmoothBilinear) {
                                wx = wx * wx * (3 - 2 * wx);
                                wy = wy * wy * (3 - 2 * wy);
                            }
                            const int sourceRow = int((y + 0.5) * current.height() / size.height());
                            const double weight = mode == FrameBlending::Disabled ? 0
                                                  : mode == FrameBlending::Simple ? 0.5
                                                  : ((sourceRow & 1) != int(odd)) ? 2.0 / 3.0
                                                                                  : 1.0 / 3.0;
                            const QRgb observed = output.pixel(x, y);
                            for (int shift : {0, 8, 16}) {
                                double light = 0;
                                for (int dy = 0; dy < 2; ++dy)
                                    for (int dx = 0; dx < 2; ++dx) {
                                        const int sx =
                                            std::clamp(left + dx, 0, current.width() - 1);
                                        const int sy =
                                            std::clamp(top + dy, 0, current.height() - 1);
                                        const double a = std::pow(
                                            ((current.pixel(sx, sy) >> shift) & 255) / 255.0, 2.2);
                                        const double b = std::pow(
                                            ((previous.pixel(sx, sy) >> shift) & 255) / 255.0, 2.2);
                                        light += (dx ? wx : 1 - wx) * (dy ? wy : 1 - wy) *
                                                 ((1 - weight) * a + weight * b);
                                    }
                                const int expected =
                                    int(std::lround(255 * std::pow(light, 1.0 / 2.2)));
                                assert(std::abs(int((observed >> shift) & 255) - expected) <= 1);
                            }
                        }
                }
            }
        }
    }
    assert(renderDisplayFrames(current, {}, {}, ScalingFilter::Bilinear, FrameBlending::Accurate,
                               false)
               .isNull());
    assert(renderDisplayFrames(current, QImage(1, 1, QImage::Format_RGB32), {7, 11},
                               ScalingFilter::Bilinear, FrameBlending::Accurate, false) ==
           filterDisplayImage(current, {7, 11}, ScalingFilter::Bilinear));
}
} // namespace

int main() {
    testParallelEdgeCaptures();
    testExpandedFilterCaptures();
    testExpandedFilterOrientation();
    testPreciseNearestSampling();
    testFilteredFrameBlending();
    QImage corners(2, 2, QImage::Format_RGB32);
    corners.setPixel(0, 0, qRgb(0, 0, 0));
    corners.setPixel(1, 0, qRgb(255, 0, 0));
    corners.setPixel(0, 1, qRgb(0, 255, 0));
    corners.setPixel(1, 1, qRgb(0, 0, 255));
    assert(filterDisplayImage({}, {4, 4}, ScalingFilter::Bilinear).isNull());
    assert(filterDisplayImage(corners, {}, ScalingFilter::SmoothBilinear).isNull());
    for (auto filter :
         {ScalingFilter::Nearest, ScalingFilter::Bilinear, ScalingFilter::SmoothBilinear}) {
        assert(filterDisplayImage(corners, corners.size(), filter) == corners);
        QImage solid(1, 1, QImage::Format_RGB32);
        solid.fill(qRgb(37, 154, 226));
        const auto scaledSolid = filterDisplayImage(solid, {7, 9}, filter);
        assert(scaledSolid.size() == QSize(7, 9));
        assert(scaledSolid.pixel(0, 0) == solid.pixel(0, 0));
        assert(scaledSolid.pixel(6, 8) == solid.pixel(0, 0));
        const auto scaled = filterDisplayImage(corners, {6, 6}, filter);
        assert(scaled.pixel(0, 0) == corners.pixel(0, 0));
        assert(scaled.pixel(5, 5) == corners.pixel(1, 1));
    }
    for (int value = 0; value < 256; ++value) {
        QImage solid(2, 2, QImage::Format_RGB32);
        solid.fill(qRgb(value, 255 - value, value));
        for (auto filter : {ScalingFilter::Bilinear, ScalingFilter::SmoothBilinear}) {
            const auto scaled = filterDisplayImage(solid, {5, 7}, filter);
            assert(scaled.pixel(2, 3) == solid.pixel(0, 0));
            assert(scaled.pixel(0, 0) == solid.pixel(0, 0));
        }
    }
    const auto nearest = filterDisplayImage(corners, {6, 6}, ScalingFilter::Nearest);
    assert(nearest.pixel(2, 2) == corners.pixel(0, 0));
    assert(nearest.pixel(3, 3) == corners.pixel(1, 1));
    for (auto filter : {ScalingFilter::Bilinear, ScalingFilter::SmoothBilinear}) {
        const auto scaled = filterDisplayImage(corners, {7, 5}, filter);
        for (int y = 0; y < scaled.height(); ++y) {
            for (int x = 0; x < scaled.width(); ++x) {
                double wx = std::clamp((x + 0.5) * 2.0 / scaled.width() - 0.5, 0.0, 1.0);
                double wy = std::clamp((y + 0.5) * 2.0 / scaled.height() - 0.5, 0.0, 1.0);
                if (filter == ScalingFilter::SmoothBilinear) {
                    wx = wx * wx * (3 - 2 * wx);
                    wy = wy * wy * (3 - 2 * wy);
                }
                const std::array<double, 3> light = {wx * (1 - wy), (1 - wx) * wy, wx * wy};
                const QRgb color = scaled.pixel(x, y);
                const std::array<int, 3> observed = {qRed(color), qGreen(color), qBlue(color)};
                for (size_t channel = 0; channel < light.size(); ++channel) {
                    const int expected =
                        int(std::lround(std::pow(light[channel], 1.0 / 2.2) * 255));
                    assert(std::abs(observed[channel] - expected) <= 1);
                }
            }
        }
        assert(scaled.pixel(3, 2) == qRgb(136, 136, 136));
    }

    const QSize native(160, 144);
    assert(displayViewport(native, {500, 400}, DisplayScaling::Integer) ==
           QRectF(90, 56, 320, 288));
    const auto aspect = displayViewport(native, {500, 400}, DisplayScaling::Aspect);
    assert(aspect == QRectF(28, 0, 444, 400));
    assert(displayViewport(native, {500, 400}, DisplayScaling::Fill) == QRectF(0, 0, 500, 400));
    // Integer factors refer to physical display pixels, including fractional scaling.
    const auto fractional = displayViewport(native, {500, 400}, DisplayScaling::Integer, 1.25);
    assert(std::abs(fractional.x() - 57.6) < 1e-10 && fractional.width() == 384);
    assert(std::abs(fractional.y() - 27.2) < 1e-10);
    assert(std::abs(fractional.height() - 345.6) < 1e-10);
    assert(displayViewport({256, 224}, {700, 600}, DisplayScaling::Integer) ==
           QRectF(94, 76, 512, 448));
    assert(displayViewport({}, {500, 400}, DisplayScaling::Integer).isEmpty());
    assert(displayViewport(native, {}, DisplayScaling::Aspect).isEmpty());

    const std::array<QRgb, 4> custom = {qRgb(1, 2, 3), qRgb(4, 5, 6), qRgb(7, 8, 9),
                                        qRgb(10, 11, 12)};
    const auto gray = monochromeColors(MonochromePalette::Grayscale, custom);
    assert(gray[0] == qRgb(255, 255, 255));
    assert(gray[1] == qRgb(170, 170, 170));
    assert(gray[2] == qRgb(85, 85, 85));
    assert(gray[3] == qRgb(0, 0, 0));
    assert(monochromeColors(MonochromePalette::Custom, custom) == custom);
    assert(monochromeColors(MonochromePalette::Lime, custom)[0] == QColor("#c6de8c").rgb());
    assert(monochromeColors(MonochromePalette::Olive, custom)[3] == QColor("#07100e").rgb());
    assert(monochromeColors(MonochromePalette::Teal, custom)[0] == QColor("#7fe2c3").rgb());
    assert(monochromeOffColor(MonochromePalette::Lime, custom[0]) == QColor("#d2e6a6").rgb());
    assert(monochromeOffColor(MonochromePalette::Grayscale, custom[0]) == qRgb(255, 255, 255));
    assert(monochromeOffColor(MonochromePalette::Olive, custom[0]) == QColor("#cfdaac").rgb());
    assert(monochromeOffColor(MonochromePalette::Teal, custom[0]) == QColor("#91ead0").rgb());
    assert(monochromeOffColor(MonochromePalette::Custom, custom[0]) == custom[0]);

    QImage black(160, 144, QImage::Format_RGB32);
    QImage white(160, 144, QImage::Format_RGB32);
    black.fill(Qt::black);
    white.fill(Qt::white);
    const auto simple = FrameBlending::Simple;
    const auto accurate = FrameBlending::Accurate;
    const auto blended = blendDisplayFrames(white, black, simple, false);
    assert(blended.pixel(0, 0) == qRgb(186, 186, 186));
    assert(blended == blendDisplayFrames(black, white, simple, true));
    assert(blendDisplayFrames(black, black, simple, false) == black);
    assert(blendDisplayFrames(white, white, accurate, true) == white);
    assert(blendDisplayFrames(white, {}, accurate, false) == white);
    assert(blendDisplayFrames(white, QImage(1, 1, QImage::Format_RGB32), accurate, false) == white);
    assert(blendDisplayFrames({}, black, simple, false).isNull());
    assert(blendDisplayFrames(white, black, FrameBlending::Disabled, true) == white);
    const auto even = blendDisplayFrames(white, black, accurate, false);
    const auto odd = blendDisplayFrames(white, black, accurate, true);
    for (int y = 0; y < white.height(); ++y) {
        assert(even.pixel(0, y) == (y & 1 ? qRgb(155, 155, 155) : qRgb(212, 212, 212)));
        assert(odd.pixel(159, y) == even.pixel(0, y ^ 1));
    }
    assert(odd == blendDisplayFrames(black, white, accurate, false));
    QImage oneChangedRow = white.copy();
    for (int x = 0; x < oneChangedRow.width(); ++x)
        oneChangedRow.setPixel(x, 0, qRgb(0, 0, 0));
    const auto partial = blendDisplayFrames(white, oneChangedRow, accurate, false);
    assert(partial.pixel(159, 0) == qRgb(212, 212, 212));
    assert(partial.pixel(159, 1) == qRgb(255, 255, 255));
    // Cover all channel values and the table's rounding independently.
    QImage ramp(256, 1, QImage::Format_RGB32);
    QImage inverse(256, 1, QImage::Format_RGB32);
    for (int x = 0; x < 256; ++x) {
        ramp.setPixel(x, 0, qRgb(x, x, x));
        inverse.setPixel(x, 0, qRgb(255 - x, 255 - x, 255 - x));
    }
    const auto blendedRamp = blendDisplayFrames(ramp, inverse, simple, false);
    const auto weightedRamp = blendDisplayFrames(ramp, inverse, accurate, true);
    for (int x = 0; x < 256; ++x) {
        const double a = std::pow(x / 255.0, 2.2);
        const double b = std::pow((255 - x) / 255.0, 2.2);
        const auto value = int(std::lround(std::pow((a + b) / 2.0, 1.0 / 2.2) * 255.0));
        assert(blendedRamp.pixel(x, 0) == qRgb(value, value, value));
        const auto weighted = int(std::lround(std::pow((a + 2 * b) / 3.0, 1.0 / 2.2) * 255.0));
        assert(weightedRamp.pixel(x, 0) == qRgb(weighted, weighted, weighted));
    }
}

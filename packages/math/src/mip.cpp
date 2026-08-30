#include <engine/math/mip.hpp>

#include <engine/math/srgb.hpp>

namespace engine::math {
namespace {

// One level down. Reads from `packed` at src_off, appends the result.
void append_box_mip(std::vector<u8>& packed, usize src_off, u32 src_w, u32 src_h,
    u32 dst_w, u32 dst_h, bool srgb) {
    const usize dst_off = packed.size();
    packed.resize(dst_off + static_cast<usize>(dst_w) * dst_h * 4);
    const u8* src = packed.data() + src_off;
    u8* dst = packed.data() + dst_off;
    for (u32 y = 0; y < dst_h; ++y) {
        for (u32 x = 0; x < dst_w; ++x) {
            // Odd dimensions clamp rather than wrap, so the last column or row
            // is sampled twice instead of bleeding in from the far edge.
            const u32 x0 = (x * 2 < src_w) ? x * 2 : src_w - 1;
            const u32 x1 = (x * 2 + 1 < src_w) ? x * 2 + 1 : src_w - 1;
            const u32 y0 = (y * 2 < src_h) ? y * 2 : src_h - 1;
            const u32 y1 = (y * 2 + 1 < src_h) ? y * 2 + 1 : src_h - 1;
            const u32 samples[4] = {
                (y0 * src_w + x0) * 4,
                (y0 * src_w + x1) * 4,
                (y1 * src_w + x0) * 4,
                (y1 * src_w + x1) * 4,
            };

            const usize i = (static_cast<usize>(y) * dst_w + x) * 4;

            if (srgb) {
                // RGB through the transfer function; alpha is not colour.
                for (u32 c = 0; c < 3; ++c) {
                    f32 acc = 0.f;
                    for (u32 s = 0; s < 4; ++s) {
                        acc += srgb_byte_to_linear(src[samples[s] + c]);
                    }
                    dst[i + c] = linear_to_srgb_byte(acc * 0.25f);
                }
            } else {
                for (u32 c = 0; c < 3; ++c) {
                    u32 acc = 0;
                    for (u32 s = 0; s < 4; ++s) {
                        acc += src[samples[s] + c];
                    }
                    dst[i + c] = static_cast<u8>(acc / 4);
                }
            }

            u32 alpha = 0;
            for (u32 s = 0; s < 4; ++s) {
                alpha += src[samples[s] + 3];
            }
            dst[i + 3] = static_cast<u8>(alpha / 4);
        }
    }
}

} // namespace

u32 mip_chain_length(u32 width, u32 height) {
    u32 levels = 1;
    u32 w = width;
    u32 h = height;
    while (w > 1 || h > 1) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        ++levels;
    }
    return levels;
}

std::vector<u8> build_rgba8_mip_chain(const void* top, u32 width, u32 height, u32 mip_count,
    bool srgb) {
    std::vector<u8> packed;
    if (!top || width == 0 || height == 0 || mip_count == 0) {
        return packed;
    }

    usize total = 0;
    {
        u32 w = width;
        u32 h = height;
        for (u32 mip = 0; mip < mip_count; ++mip) {
            total += static_cast<usize>(w) * h * 4;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
    }

    packed.reserve(total);
    const usize top_bytes = static_cast<usize>(width) * height * 4;
    packed.assign(static_cast<const u8*>(top), static_cast<const u8*>(top) + top_bytes);

    u32 src_w = width;
    u32 src_h = height;
    usize src_off = 0;
    for (u32 mip = 1; mip < mip_count; ++mip) {
        const u32 dst_w = src_w > 1 ? src_w / 2 : 1;
        const u32 dst_h = src_h > 1 ? src_h / 2 : 1;
        append_box_mip(packed, src_off, src_w, src_h, dst_w, dst_h, srgb);
        src_off += static_cast<usize>(src_w) * src_h * 4;
        src_w = dst_w;
        src_h = dst_h;
    }
    return packed;
}

} // namespace engine::math

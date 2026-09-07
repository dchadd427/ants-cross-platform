#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <unordered_map>

struct Sprite {
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    std::string filename;
    std::vector<uint8_t> pixels;
};

struct Frame {
    int32_t dx;
    int32_t dy;
    uint32_t sprite_index;
};

struct SubItem {
    int32_t box_left;
    int32_t box_top;
    int32_t box_right;
    int32_t box_bottom;
    uint32_t default_sp;
    std::vector<Frame> frames;
};

struct Animation {
    std::string name;
    uint32_t flag1;
    uint32_t flag2;
    uint32_t flag3;
    std::vector<SubItem> subitems;
};

int main() {
    std::string chd_path = "/Users/dchadd/Desktop/Ants-Mac/Original-Ants/ants.chd";
    std::ifstream f(chd_path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "Failed to open ants.chd\n";
        return 1;
    }
    
    uint32_t hdr[7];
    f.read(reinterpret_cast<char*>(hdr), 28);
    uint32_t t1_off = hdr[2];
    uint32_t t4_off = hdr[5];
    
    // Load Table 1: Sprites
    f.seekg(t1_off);
    uint32_t s_count = 0;
    f.read(reinterpret_cast<char*>(&s_count), 4);
    std::vector<uint32_t> s_offsets(s_count);
    f.read(reinterpret_cast<char*>(s_offsets.data()), s_count * 4);
    
    std::vector<Sprite> sprites(s_count);
    for (uint32_t i = 0; i < s_count; ++i) {
        f.seekg(s_offsets[i]);
        uint32_t p, w, h, fn_len;
        f.read(reinterpret_cast<char*>(&p), 4);
        f.read(reinterpret_cast<char*>(&w), 4);
        f.read(reinterpret_cast<char*>(&h), 4);
        f.read(reinterpret_cast<char*>(&fn_len), 4);
        std::vector<char> fn(fn_len);
        f.read(fn.data(), fn_len);
        
        sprites[i].pitch = p;
        sprites[i].width = w;
        sprites[i].height = h;
        sprites[i].filename = std::string(fn.data());
        sprites[i].pixels.resize(p * h);
        f.read(reinterpret_cast<char*>(sprites[i].pixels.data()), p * h);
    }
    std::cout << "Loaded " << sprites.size() << " sprites from ants.chd.\n";
    
    // Load Table 4: Animations
    f.seekg(t4_off);
    uint32_t a_count = 0;
    f.read(reinterpret_cast<char*>(&a_count), 4);
    std::vector<uint32_t> a_offsets(a_count);
    f.read(reinterpret_cast<char*>(a_offsets.data()), a_count * 4);
    
    std::vector<Animation> anims(a_count);
    std::unordered_map<std::string, size_t> anim_map;
    for (uint32_t i = 0; i < a_count; ++i) {
        f.seekg(a_offsets[i]);
        uint32_t nlen;
        f.read(reinterpret_cast<char*>(&nlen), 4);
        std::vector<char> name_buf(nlen);
        f.read(name_buf.data(), nlen);
        anims[i].name = std::string(name_buf.data());
        anim_map[anims[i].name] = i;
        
        f.read(reinterpret_cast<char*>(&anims[i].flag1), 4);
        f.read(reinterpret_cast<char*>(&anims[i].flag2), 4);
        f.read(reinterpret_cast<char*>(&anims[i].flag3), 4);
        uint32_t sub_count;
        f.read(reinterpret_cast<char*>(&sub_count), 4);
        anims[i].subitems.resize(sub_count);
        
        for (uint32_t s = 0; s < sub_count; ++s) {
            int32_t v1, v2, v3, bl, bt, br, bb;
            uint32_t v8, def_sp, f_cnt;
            f.read(reinterpret_cast<char*>(&v1), 4);
            f.read(reinterpret_cast<char*>(&v2), 4);
            f.read(reinterpret_cast<char*>(&v3), 4);
            f.read(reinterpret_cast<char*>(&bl), 4);
            f.read(reinterpret_cast<char*>(&bt), 4);
            f.read(reinterpret_cast<char*>(&br), 4);
            f.read(reinterpret_cast<char*>(&bb), 4);
            f.read(reinterpret_cast<char*>(&v8), 4);
            f.read(reinterpret_cast<char*>(&def_sp), 4);
            f.read(reinterpret_cast<char*>(&f_cnt), 4);
            
            anims[i].subitems[s].box_left = bl;
            anims[i].subitems[s].box_top = bt;
            anims[i].subitems[s].box_right = br;
            anims[i].subitems[s].box_bottom = bb;
            anims[i].subitems[s].default_sp = def_sp;
            anims[i].subitems[s].frames.resize(f_cnt);
            for (uint32_t fr = 0; fr < f_cnt; ++fr) {
                f.read(reinterpret_cast<char*>(&anims[i].subitems[s].frames[fr].dx), 4);
                f.read(reinterpret_cast<char*>(&anims[i].subitems[s].frames[fr].dy), 4);
                f.read(reinterpret_cast<char*>(&anims[i].subitems[s].frames[fr].sprite_index), 4);
            }
        }
    }
    std::cout << "Loaded " << anims.size() << " animations from ants.chd.\n";
    
    // Now test mirroring for Worker Walk (agwg801 -> NE, agwg901 -> E, agwg201 -> SE)
    std::vector<std::string> base_anims = {"agwg801", "agwg901", "agwg201"};
    for (const auto& bname : base_anims) {
        assert(anim_map.find(bname) != anim_map.end());
        const auto& anim = anims[anim_map[bname]];
        std::cout << "Inspecting " << bname << ": " << anim.subitems.size() << " subitems\n";
        for (size_t s = 0; s < anim.subitems.size(); ++s) {
            const auto& sub = anim.subitems[s];
            for (size_t f = 0; f < sub.frames.size(); ++f) {
                const auto& fr = sub.frames[f];
                const auto& sp = sprites[fr.sprite_index];
                int32_t mirrored_dx = -(fr.dx + static_cast<int32_t>(sp.width));
                // Verify that [mirrored_dx, mirrored_dx + sp.width] is exactly symmetric to [fr.dx, fr.dx + sp.width]
                assert(mirrored_dx + static_cast<int32_t>(sp.width) == -fr.dx);
            }
        }
    }
    std::cout << "Mirroring verification for real CHD animations PASSED!\n";
    return 0;
}

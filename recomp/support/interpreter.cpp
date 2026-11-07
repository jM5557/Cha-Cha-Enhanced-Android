// Interpreter for guest code that differs from the executable: functions whose instructions a cheat patched, and the
// code caves cheats add (ppc_context.h, "patched code"). The recompiled functions stay the fast path; this runs only
// what the cheat engine flagged, reading every instruction from guest memory as it goes.
//
// Every operation mirrors what recomp/src/emitter.cpp emits for it (same helpers from ppc_context.h, same lane rules,
// same host-safe divide and trap handling), so a function behaves the same interpreted or recompiled - checked by
// interp_selftest against the emitted self-test functions. Control flow follows the emitter's model too:
//   bl / bcl / bctrl / bclrl  call: a recompiled function through call_indirect, any other address (a code cave) by
//                             interpreting it, on the host stack; execution then continues after the call
//   bclr                      return from this interpretation (the emitter's `return`)
//   b / bc / bctr             a jump: to another function's entry it is a tail call (call, then return - the same
//                             thing, LR untouched), anywhere else the interpretation continues there (in-body
//                             branches, switch table slots, jumps into and back out of code caves)
// Import call sites carry a meaningless displacement (the stubs are out of branch range), so their real target comes
// from set_interpreter_branch_targets (the runtime fills it from the loader's import call sites).
#include "espresso.h"
#include "ppc_context.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

namespace mh3u::ppc {
namespace {

std::unordered_map<uint32_t, uint32_t>& branch_targets() {
    static std::unordered_map<uint32_t, uint32_t> m;
    return m;
}

[[noreturn]] void unsupported(const Insn& in, uint32_t pc) {
    std::fprintf(stderr, "interpreter: unsupported instruction %08X (%s) at %08X - patched code (a cheat) uses an "
                         "instruction the recompiler never needed\n",
                 in.raw, format(in, pc).c_str(), pc);
    std::fflush(stderr);
    std::_Exit(2);
}

bool branch_cond(Context& ctx, const Insn& in) {
    const uint32_t bo = in.d, bi = in.a;
    bool ok = true;
    if (!(bo & 4)) {
        --ctx.ctr;
        ok = (bo & 2) ? ctx.ctr == 0 : ctx.ctr != 0;
    }
    if (!(bo & 16)) ok = ok && (cr_bit(ctx, bi) != 0) == ((bo & 8) != 0);
    return ok;
}

// a call from interpreted code (LR already set by the caller)
void call(Context& ctx, uint8_t* base, uint32_t target) {
    if (lookup_function(target)) call_indirect(ctx, base, target);
    else interpret(ctx, base, target);
}

}  // namespace

bool interpretable(uint32_t word) {
    const Insn in = decode(word);
    const uint32_t prim = word >> 26;
    const bool fp_op = prim == 59 || prim == 63 || prim == 4;
    if (!in.valid() || in.oe || (in.rc && fp_op)) return false;
    switch (in.op) {
        case Op::mfspr: case Op::mtspr: return in.spr == 1 || in.spr == 8 || in.spr == 9 || (in.spr >= 912 && in.spr <= 919);
        // what interpret() has no case for (the recompiler never needed them either)
        case Op::mcrxr: case Op::lswx: case Op::stswx: case Op::mcrfs: case Op::mffs: case Op::mtfsb0: case Op::mtfsb1:
        case Op::mtfsf: case Op::mtfsfi: case Op::psq_l: case Op::psq_lu: case Op::psq_lux: case Op::psq_lx: case Op::psq_st:
        case Op::psq_stu: case Op::psq_stux: case Op::psq_stx: case Op::ps_abs: case Op::ps_add: case Op::ps_cmpo0:
        case Op::ps_cmpo1: case Op::ps_cmpu0: case Op::ps_cmpu1: case Op::ps_div: case Op::ps_madd: case Op::ps_madds0:
        case Op::ps_madds1: case Op::ps_mr: case Op::ps_msub: case Op::ps_mul: case Op::ps_muls0: case Op::ps_muls1:
        case Op::ps_nabs: case Op::ps_neg: case Op::ps_nmadd: case Op::ps_nmsub: case Op::ps_res: case Op::ps_rsqrte:
        case Op::ps_sel: case Op::ps_sub: case Op::ps_sum0: case Op::ps_sum1: case Op::eciwx: case Op::ecowx:
        case Op::mfmsr: case Op::mfsr: case Op::mfsrin: case Op::mftb: case Op::mtmsr: case Op::mtsr: case Op::mtsrin:
        case Op::rfi: case Op::sc: case Op::tlbie: case Op::tlbsync:
            return false;
        case Op::bcctr: return (in.d & 4) != 0;
        default: return true;
    }
}

void set_interpreter_branch_targets(const uint32_t* sites, const uint32_t* targets, size_t count) {
    auto& m = branch_targets();
    m.clear();
    m.reserve(count);
    for (size_t i = 0; i < count; ++i) m[sites[i]] = targets[i];
}

void interpret(Context& ctx, uint8_t* base, const uint32_t entry) {
    uint32_t pc = entry;
    const auto& remap = branch_targets();
    for (;;) {
        const Insn in = decode(ld32(base, pc));
        uint32_t next = pc + 4;
        auto& r = ctx.r;
        auto& f = ctx.f;
        const int d = in.d, a = in.a, b = in.b;
        const uint32_t ra0 = a ? r[a] : 0u;
        const uint32_t uimm = static_cast<uint32_t>(in.imm);
        const auto sx = [&](int n) { return static_cast<int32_t>(r[n]); };
        const auto carry_add = [&](uint32_t x, uint32_t y, uint32_t z) {
            const uint64_t s = uint64_t{x} + uint64_t{y} + uint64_t{z};
            r[d] = static_cast<uint32_t>(s);
            ctx.xer_ca = static_cast<uint8_t>(s >> 32);
        };
        const auto ea_d = [&](bool update) { return (update ? r[a] : ra0) + uimm; };
        const auto ea_x = [&](bool update) { return (update ? r[a] : ra0) + r[b]; };
        const auto fset = [&](double v, bool single) {
            if (single) {
                const double s = round_single(v);
                f[d].ps0 = s;
                f[d].ps1 = s;
            } else {
                f[d].ps0 = v;
            }
        };
        const double fa = f[a].ps0, fb = f[b].ps0, fc = f[in.c].ps0;
        const uint32_t prim = in.raw >> 26;
        const bool fp_op = prim == 59 || prim == 63 || prim == 4;
        if (!in.valid() || in.oe || (in.rc && fp_op)) unsupported(in, pc);

        // loads/stores: (fn, sign-extend 16, update, indexed)
#define LD(fn, sign16, update, indexed)                                                              \
    {                                                                                                \
        const uint32_t ea = (indexed) ? ea_x(update) : ea_d(update);                                 \
        r[d] = (sign16) ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(fn(base, ea)))) \
                        : static_cast<uint32_t>(fn(base, ea));                                       \
        if (update) r[a] = ea;                                                                       \
        break;                                                                                       \
    }
#define ST(fn, cast, update, indexed)                                \
    {                                                                \
        const uint32_t ea = (indexed) ? ea_x(update) : ea_d(update); \
        fn(base, ea, static_cast<cast>(r[d]));                       \
        if (update) r[a] = ea;                                       \
        break;                                                       \
    }
#define LF(single, update, indexed)                                  \
    {                                                                \
        const uint32_t ea = (indexed) ? ea_x(update) : ea_d(update); \
        if (single) {                                                \
            const double v = ldf32(base, ea);                        \
            f[d].ps0 = v;                                            \
            f[d].ps1 = v;                                            \
        } else {                                                     \
            f[d].ps0 = ldf64(base, ea);                              \
        }                                                            \
        if (update) r[a] = ea;                                       \
        break;                                                       \
    }
#define SF(single, update, indexed)                                  \
    {                                                                \
        const uint32_t ea = (indexed) ? ea_x(update) : ea_d(update); \
        if (single) stf32(base, ea, static_cast<float>(f[d].ps0));   \
        else stf64(base, ea, f[d].ps0);                              \
        if (update) r[a] = ea;                                       \
        break;                                                       \
    }

        switch (in.op) {
            // ---- integer arithmetic
            case Op::addi: r[d] = ra0 + uimm; break;
            case Op::addis: r[d] = ra0 + (uimm << 16); break;
            case Op::addic: case Op::addic_rc: carry_add(r[a], uimm, 0); break;
            case Op::subfic: carry_add(~r[a], uimm, 1); break;
            case Op::mulli: r[d] = static_cast<uint32_t>(int64_t{sx(a)} * in.imm); break;
            case Op::add: r[d] = r[a] + r[b]; break;
            case Op::addc: carry_add(r[a], r[b], 0); break;
            case Op::adde: carry_add(r[a], r[b], ctx.xer_ca); break;
            case Op::addze: carry_add(r[a], 0, ctx.xer_ca); break;
            case Op::addme: carry_add(r[a], 0xFFFFFFFFu, ctx.xer_ca); break;
            case Op::subf: r[d] = r[b] - r[a]; break;
            case Op::subfc: carry_add(~r[a], r[b], 1); break;
            case Op::subfe: carry_add(~r[a], r[b], ctx.xer_ca); break;
            case Op::subfze: carry_add(~r[a], 0, ctx.xer_ca); break;
            case Op::subfme: carry_add(~r[a], 0xFFFFFFFFu, ctx.xer_ca); break;
            case Op::neg: r[d] = 0u - r[a]; break;
            case Op::mullw: r[d] = static_cast<uint32_t>(int64_t{sx(a)} * int64_t{sx(b)}); break;
            case Op::mulhw: r[d] = static_cast<uint32_t>((int64_t{sx(a)} * int64_t{sx(b)}) >> 32); break;
            case Op::mulhwu: r[d] = static_cast<uint32_t>((uint64_t{r[a]} * uint64_t{r[b]}) >> 32); break;
            case Op::divw: {
                const int32_t x = sx(a), y = sx(b);
                r[d] = (y == 0 || (x == INT32_MIN && y == -1)) ? (x < 0 ? 0xFFFFFFFFu : 0u) : static_cast<uint32_t>(x / y);
                break;
            }
            case Op::divwu: r[d] = r[b] == 0 ? 0u : r[a] / r[b]; break;
            // ---- compare
            case Op::cmp: cr_set_s(ctx.cr[in.crf], sx(a), sx(b), ctx.xer_so); break;
            case Op::cmpl: cr_set_u(ctx.cr[in.crf], r[a], r[b], ctx.xer_so); break;
            case Op::cmpi: cr_set_s(ctx.cr[in.crf], sx(a), in.imm, ctx.xer_so); break;
            case Op::cmpli: cr_set_u(ctx.cr[in.crf], r[a], uimm, ctx.xer_so); break;
            // ---- logical (dest rA, source rS = d)
            case Op::and_: r[a] = r[d] & r[b]; break;
            case Op::andc: r[a] = r[d] & ~r[b]; break;
            case Op::or_: r[a] = r[d] | r[b]; break;
            case Op::orc: r[a] = r[d] | ~r[b]; break;
            case Op::xor_: r[a] = r[d] ^ r[b]; break;
            case Op::nor: r[a] = ~(r[d] | r[b]); break;
            case Op::nand: r[a] = ~(r[d] & r[b]); break;
            case Op::eqv: r[a] = ~(r[d] ^ r[b]); break;
            case Op::andi_rc: r[a] = r[d] & uimm; break;
            case Op::andis_rc: r[a] = r[d] & (uimm << 16); break;
            case Op::ori: r[a] = r[d] | uimm; break;
            case Op::oris: r[a] = r[d] | (uimm << 16); break;
            case Op::xori: r[a] = r[d] ^ uimm; break;
            case Op::xoris: r[a] = r[d] ^ (uimm << 16); break;
            case Op::cntlzw: r[a] = static_cast<uint32_t>(std::countl_zero(r[d])); break;
            case Op::extsb: r[a] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(r[d]))); break;
            case Op::extsh: r[a] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(r[d]))); break;
            case Op::slw: {
                const uint32_t n = r[b] & 63;
                r[a] = n > 31 ? 0u : r[d] << n;
                break;
            }
            case Op::srw: {
                const uint32_t n = r[b] & 63;
                r[a] = n > 31 ? 0u : r[d] >> n;
                break;
            }
            case Op::sraw: {
                const uint32_t n = r[b] & 63;
                const int32_t v = sx(d);
                if (n > 31) {
                    r[a] = static_cast<uint32_t>(v >> 31);
                    ctx.xer_ca = static_cast<uint8_t>(v < 0);
                } else {
                    r[a] = static_cast<uint32_t>(v >> n);
                    ctx.xer_ca = static_cast<uint8_t>(v < 0 && (static_cast<uint32_t>(v) & ((1u << n) - 1)) != 0);
                }
                break;
            }
            case Op::srawi: {
                const int32_t v = sx(d);
                r[a] = static_cast<uint32_t>(v >> b);
                ctx.xer_ca = static_cast<uint8_t>(v < 0 && (static_cast<uint32_t>(v) & ((1u << b) - 1)) != 0);
                break;
            }
            case Op::rlwinm: r[a] = rotl32(r[d], b) & mask32(in.c, in.e); break;
            case Op::rlwnm: r[a] = rotl32(r[d], r[b]) & mask32(in.c, in.e); break;
            case Op::rlwimi: {
                const uint32_t m = mask32(in.c, in.e);
                r[a] = (rotl32(r[d], b) & m) | (r[a] & ~m);
                break;
            }
            // ---- integer load/store
            case Op::lbz: LD(ld8, false, false, false)
            case Op::lbzu: LD(ld8, false, true, false)
            case Op::lbzx: LD(ld8, false, false, true)
            case Op::lbzux: LD(ld8, false, true, true)
            case Op::lhz: LD(ld16, false, false, false)
            case Op::lhzu: LD(ld16, false, true, false)
            case Op::lhzx: LD(ld16, false, false, true)
            case Op::lhzux: LD(ld16, false, true, true)
            case Op::lha: LD(ld16, true, false, false)
            case Op::lhau: LD(ld16, true, true, false)
            case Op::lhax: LD(ld16, true, false, true)
            case Op::lhaux: LD(ld16, true, true, true)
            case Op::lwz: LD(ld32, false, false, false)
            case Op::lwzu: LD(ld32, false, true, false)
            case Op::lwzx: LD(ld32, false, false, true)
            case Op::lwzux: LD(ld32, false, true, true)
            case Op::lhbrx: LD(ld16_le, false, false, true)
            case Op::lwbrx: LD(ld32_le, false, false, true)
            case Op::stb: ST(st8, uint8_t, false, false)
            case Op::stbu: ST(st8, uint8_t, true, false)
            case Op::stbx: ST(st8, uint8_t, false, true)
            case Op::stbux: ST(st8, uint8_t, true, true)
            case Op::sth: ST(st16, uint16_t, false, false)
            case Op::sthu: ST(st16, uint16_t, true, false)
            case Op::sthx: ST(st16, uint16_t, false, true)
            case Op::sthux: ST(st16, uint16_t, true, true)
            case Op::stw: ST(st32, uint32_t, false, false)
            case Op::stwu: ST(st32, uint32_t, true, false)
            case Op::stwx: ST(st32, uint32_t, false, true)
            case Op::stwux: ST(st32, uint32_t, true, true)
            case Op::sthbrx: ST(st16_le, uint16_t, false, true)
            case Op::stwbrx: ST(st32_le, uint32_t, false, true)
            case Op::lwarx: r[d] = lwarx(ctx, base, ea_x(false)); break;
            case Op::stwcx_rc: stwcx(ctx, base, ea_x(false), r[d]); break;
            case Op::lswi: lswi(ctx, base, ra0, static_cast<uint32_t>(d), b ? static_cast<uint32_t>(b) : 32u); break;
            case Op::stswi: stswi(ctx, base, ra0, static_cast<uint32_t>(d), b ? static_cast<uint32_t>(b) : 32u); break;
            case Op::tw: if (trap_cond(r[a], r[b], static_cast<uint32_t>(d))) trap(pc); break;
            case Op::twi: if (trap_cond(r[a], uimm, static_cast<uint32_t>(d))) trap(pc); break;
            case Op::lmw: {
                uint32_t ea = ea_d(false);
                for (int i = d; i < 32; ++i, ea += 4) r[i] = ld32(base, ea);
                break;
            }
            case Op::stmw: {
                uint32_t ea = ea_d(false);
                for (int i = d; i < 32; ++i, ea += 4) st32(base, ea, r[i]);
                break;
            }
            // ---- FP load/store
            case Op::lfs: LF(true, false, false)
            case Op::lfsu: LF(true, true, false)
            case Op::lfsx: LF(true, false, true)
            case Op::lfsux: LF(true, true, true)
            case Op::lfd: LF(false, false, false)
            case Op::lfdu: LF(false, true, false)
            case Op::lfdx: LF(false, false, true)
            case Op::lfdux: LF(false, true, true)
            case Op::stfs: SF(true, false, false)
            case Op::stfsu: SF(true, true, false)
            case Op::stfsx: SF(true, false, true)
            case Op::stfsux: SF(true, true, true)
            case Op::stfd: SF(false, false, false)
            case Op::stfdu: SF(false, true, false)
            case Op::stfdx: SF(false, false, true)
            case Op::stfdux: SF(false, true, true)
            case Op::stfiwx: st32(base, ea_x(false), static_cast<uint32_t>(std::bit_cast<uint64_t>(f[d].ps0))); break;
            // ---- floating point (double ops write ps0; single ops round and write both lanes)
            case Op::fadd: fset(fa + fb, false); break;
            case Op::fadds: fset(fa + fb, true); break;
            case Op::fsub: fset(fa - fb, false); break;
            case Op::fsubs: fset(fa - fb, true); break;
            case Op::fmul: fset(fa * fc, false); break;
            case Op::fmuls: fset(fa * fc, true); break;
            case Op::fdiv: fset(fa / fb, false); break;
            case Op::fdivs: fset(fa / fb, true); break;
            case Op::fmadd: fset(std::fma(fa, fc, fb), false); break;
            case Op::fmadds: fset(std::fma(fa, fc, fb), true); break;
            case Op::fmsub: fset(std::fma(fa, fc, -fb), false); break;
            case Op::fmsubs: fset(std::fma(fa, fc, -fb), true); break;
            case Op::fnmadd: fset(-std::fma(fa, fc, fb), false); break;
            case Op::fnmadds: fset(-std::fma(fa, fc, fb), true); break;
            case Op::fnmsub: fset(-std::fma(fa, fc, -fb), false); break;
            case Op::fnmsubs: fset(-std::fma(fa, fc, -fb), true); break;
            case Op::fsel: fset(fa >= 0.0 ? fc : fb, false); break;
            case Op::fsqrt: fset(std::sqrt(fb), false); break;
            case Op::fsqrts: fset(std::sqrt(fb), true); break;
            case Op::fres: fset(1.0 / fb, true); break;
            case Op::frsqrte: fset(1.0 / std::sqrt(fb), false); break;
            case Op::fmr: f[d].ps0 = fb; break;
            case Op::fneg: f[d].ps0 = -fb; break;
            case Op::fabs: f[d].ps0 = std::fabs(fb); break;
            case Op::fnabs: f[d].ps0 = -std::fabs(fb); break;
            case Op::frsp: fset(fb, true); break;
            case Op::fctiwz: f[d].ps0 = fctiw_bits(fb, true); break;
            case Op::fctiw: f[d].ps0 = fctiw_bits(fb, false); break;
            case Op::fcmpu: case Op::fcmpo: cr_set_f(ctx.cr[in.crf], fa, fb); break;
            // ---- paired singles used by GHS prologues/epilogues
            case Op::ps_merge00: { const double t0 = f[a].ps0, t1 = f[b].ps0; f[d].ps0 = t0; f[d].ps1 = t1; break; }
            case Op::ps_merge01: { const double t0 = f[a].ps0, t1 = f[b].ps1; f[d].ps0 = t0; f[d].ps1 = t1; break; }
            case Op::ps_merge10: { const double t0 = f[a].ps1, t1 = f[b].ps0; f[d].ps0 = t0; f[d].ps1 = t1; break; }
            case Op::ps_merge11: { const double t0 = f[a].ps1, t1 = f[b].ps1; f[d].ps0 = t0; f[d].ps1 = t1; break; }
            // ---- condition register
            case Op::crand: cr_bit(ctx, d) = static_cast<uint8_t>(cr_bit(ctx, a) & cr_bit(ctx, b)); break;
            case Op::crandc: cr_bit(ctx, d) = static_cast<uint8_t>(cr_bit(ctx, a) & !cr_bit(ctx, b)); break;
            case Op::cror: cr_bit(ctx, d) = static_cast<uint8_t>(cr_bit(ctx, a) | cr_bit(ctx, b)); break;
            case Op::crorc: cr_bit(ctx, d) = static_cast<uint8_t>(cr_bit(ctx, a) | !cr_bit(ctx, b)); break;
            case Op::crxor: cr_bit(ctx, d) = static_cast<uint8_t>(cr_bit(ctx, a) ^ cr_bit(ctx, b)); break;
            case Op::creqv: cr_bit(ctx, d) = static_cast<uint8_t>(!(cr_bit(ctx, a) ^ cr_bit(ctx, b))); break;
            case Op::crnand: cr_bit(ctx, d) = static_cast<uint8_t>(!(cr_bit(ctx, a) & cr_bit(ctx, b))); break;
            case Op::crnor: cr_bit(ctx, d) = static_cast<uint8_t>(!(cr_bit(ctx, a) | cr_bit(ctx, b))); break;
            case Op::mcrf: ctx.cr[in.crf] = ctx.cr[in.crfs]; break;
            case Op::mfcr: r[d] = cr_pack(ctx); break;
            case Op::mtcrf: cr_unpack(ctx, r[d], in.fxm); break;
            // ---- special registers
            case Op::mfspr:
                if (in.spr == 8) r[d] = ctx.lr;
                else if (in.spr == 9) r[d] = ctx.ctr;
                else if (in.spr == 1) r[d] = (uint32_t{ctx.xer_so} << 31) | (uint32_t{ctx.xer_ov} << 30) | (uint32_t{ctx.xer_ca} << 29);
                else if (in.spr >= 912 && in.spr <= 919) r[d] = ctx.gqr[in.spr - 912];
                else unsupported(in, pc);
                break;
            case Op::mtspr:
                if (in.spr == 8) ctx.lr = r[d];
                else if (in.spr == 9) ctx.ctr = r[d];
                else if (in.spr == 1) {
                    ctx.xer_so = static_cast<uint8_t>(r[d] >> 31);
                    ctx.xer_ov = static_cast<uint8_t>((r[d] >> 30) & 1);
                    ctx.xer_ca = static_cast<uint8_t>((r[d] >> 29) & 1);
                } else if (in.spr >= 912 && in.spr <= 919) ctx.gqr[in.spr - 912] = r[d];
                else unsupported(in, pc);
                break;
            // ---- cache / ordering
            case Op::sync: case Op::isync: case Op::eieio: case Op::dcbf: case Op::dcbst: case Op::dcbt: case Op::dcbtst:
            case Op::dcbi: case Op::icbi:
                break;
            case Op::dcbz: case Op::dcbz_l: std::memset(base + ((ra0 + r[b]) & ~31u), 0, 32); break;
            // ---- branches
            case Op::b: {
                uint32_t t = in.aa ? uimm : pc + uimm;
                bool fixed = false;  // a site whose target the loader knows (import, $UNDEF): always a call
                if (auto it = remap.find(pc); it != remap.end()) {
                    t = it->second;
                    fixed = true;
                }
                if (fixed) {  // the import stub (import table) or $UNDEF's 0 (reported), as emitted code calls them
                    if (in.lk) ctx.lr = pc + 4;
                    call_indirect(ctx, base, t);
                    if (!in.lk) return;
                } else if (in.lk) {
                    ctx.lr = pc + 4;
                    if (t != pc + 4) call(ctx, base, t);  // else the get-PC idiom: LR only
                } else if (t != entry && lookup_function(t)) {
                    call(ctx, base, t);  // tail call
                    return;
                } else {
                    next = t;
                }
                break;
            }
            case Op::bc: {
                const uint32_t t = in.aa ? uimm : pc + uimm;
                if (!branch_cond(ctx, in)) break;
                if (in.lk) {
                    ctx.lr = pc + 4;
                    call(ctx, base, t);
                } else if (t != entry && lookup_function(t)) {
                    call(ctx, base, t);
                    return;
                } else {
                    next = t;
                }
                break;
            }
            case Op::bclr: {
                if (!branch_cond(ctx, in)) break;
                if (in.lk) {  // call through LR: the target is the old LR, the new LR is the return address
                    const uint32_t t = ctx.lr;
                    ctx.lr = pc + 4;
                    call(ctx, base, t);
                    break;
                }
                return;
            }
            case Op::bcctr: {
                Insn plain = in;
                plain.d |= 4;  // bcctr never decrements CTR (the emitter rejects that invalid form)
                if (!branch_cond(ctx, plain)) break;
                const uint32_t t = ctx.ctr;
                if (in.lk) {
                    ctx.lr = pc + 4;
                    call(ctx, base, t);
                } else if (t != entry && lookup_function(t)) {
                    call(ctx, base, t);  // indirect tail jump
                    return;
                } else {
                    next = t;  // switch table slot, or a jump within interpreted code
                }
                break;
            }
            default: unsupported(in, pc);
        }
#undef LD
#undef ST
#undef LF
#undef SF
        if (in.rc && !fp_op) {
            const bool dest_a = in.op == Op::and_ || in.op == Op::andc || in.op == Op::or_ || in.op == Op::orc || in.op == Op::xor_ ||
                                in.op == Op::nor || in.op == Op::nand || in.op == Op::eqv || in.op == Op::cntlzw ||
                                in.op == Op::extsb || in.op == Op::extsh || in.op == Op::slw || in.op == Op::srw ||
                                in.op == Op::sraw || in.op == Op::srawi || in.op == Op::rlwinm || in.op == Op::rlwnm ||
                                in.op == Op::rlwimi;
            cr_set_s(ctx.cr[0], sx(dest_a ? a : d), 0, ctx.xer_so);
        } else if (in.op == Op::andi_rc || in.op == Op::andis_rc) {
            cr_set_s(ctx.cr[0], sx(a), 0, ctx.xer_so);
        } else if (in.op == Op::addic_rc) {
            cr_set_s(ctx.cr[0], sx(d), 0, ctx.xer_so);
        }
        pc = next;
    }
}

}  // namespace mh3u::ppc

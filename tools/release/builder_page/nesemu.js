// Headless NES for the builder page: a line-for-line port of tools/nesemu/nes.py
// and py65's MPU6502 (the CPU nes.py uses), so it produces identical state.
// Memory access order and cycle accounting follow py65 exactly (including its
// table quirks, e.g. DEC abs = 3 cycles, unimplemented opcodes = 2-byte, 0-cycle
// no-ops) because Zelda's NMI timing depends on them. Verified frame-by-frame
// against the Python emulator by tools/nesdata/test_nesemu_js.py.
// T-419 Task 10.

export const CYCLES_PER_FRAME = 29781;
export const VBLANK_CYCLES = 2273;
export const CYCLES_PER_LINE = 113.667;
export const SPRITE0_LINE = 40;
export const BUTTONS = ["A", "B", "Select", "Start", "Up", "Down", "Left", "Right"];

// [name, mode, cycles, extracycles] for opcodes 0..255, generated from py65 1.2.0.
const OPS = [
  ["BRK","imp",7,0], ["ORA","inx",6,0], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["ORA","zpg",3,0], ["ASL","zpg",5,0], ["???","imp",0,0],
  ["PHP","imp",3,0], ["ORA","imm",2,0], ["ASL","acc",2,0], ["???","imp",0,0],
  ["???","imp",0,0], ["ORA","abs",4,0], ["ASL","abs",6,0], ["???","imp",0,0],
  ["BPL","rel",2,2], ["ORA","iny",5,1], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["ORA","zpx",4,0], ["ASL","zpx",6,0], ["???","imp",0,0],
  ["CLC","imp",2,0], ["ORA","aby",4,1], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["ORA","abx",4,1], ["ASL","abx",7,0], ["???","imp",0,0],
  ["JSR","abs",6,0], ["AND","inx",6,0], ["???","imp",0,0], ["???","imp",0,0],
  ["BIT","zpg",3,0], ["AND","zpg",3,0], ["ROL","zpg",5,0], ["???","imp",0,0],
  ["PLP","imp",4,0], ["AND","imm",2,0], ["ROL","acc",2,0], ["???","imp",0,0],
  ["BIT","abs",4,0], ["AND","abs",4,0], ["ROL","abs",6,0], ["???","imp",0,0],
  ["BMI","rel",2,2], ["AND","iny",5,1], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["AND","zpx",4,0], ["ROL","zpx",6,0], ["???","imp",0,0],
  ["SEC","imp",2,0], ["AND","aby",4,1], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["AND","abx",4,1], ["ROL","abx",7,0], ["???","imp",0,0],
  ["RTI","imp",6,0], ["EOR","inx",6,0], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["EOR","zpg",3,0], ["LSR","zpg",5,0], ["???","imp",0,0],
  ["PHA","imp",3,0], ["EOR","imm",2,0], ["LSR","acc",2,0], ["???","imp",0,0],
  ["JMP","abs",3,0], ["EOR","abs",4,0], ["LSR","abs",6,0], ["???","imp",0,0],
  ["BVC","rel",2,2], ["EOR","iny",5,1], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["EOR","zpx",4,0], ["LSR","zpx",6,0], ["???","imp",0,0],
  ["CLI","imp",2,0], ["EOR","aby",4,1], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["EOR","abx",4,1], ["LSR","abx",7,0], ["???","imp",0,0],
  ["RTS","imp",6,0], ["ADC","inx",6,0], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["ADC","zpg",3,0], ["ROR","zpg",5,0], ["???","imp",0,0],
  ["PLA","imp",4,0], ["ADC","imm",2,0], ["ROR","acc",2,0], ["???","imp",0,0],
  ["JMP","ind",5,0], ["ADC","abs",4,0], ["ROR","abs",6,0], ["???","imp",0,0],
  ["BVS","rel",2,2], ["ADC","iny",5,1], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["ADC","zpx",4,0], ["ROR","zpx",6,0], ["???","imp",0,0],
  ["SEI","imp",2,0], ["ADC","aby",4,1], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["ADC","abx",4,1], ["ROR","abx",7,0], ["???","imp",0,0],
  ["???","imp",0,0], ["STA","inx",6,0], ["???","imp",0,0], ["???","imp",0,0],
  ["STY","zpg",3,0], ["STA","zpg",3,0], ["STX","zpg",3,0], ["???","imp",0,0],
  ["DEY","imp",2,0], ["???","imp",0,0], ["TXA","imp",2,0], ["???","imp",0,0],
  ["STY","abs",4,0], ["STA","abs",4,0], ["STX","abs",4,0], ["???","imp",0,0],
  ["BCC","rel",2,2], ["STA","iny",6,0], ["???","imp",0,0], ["???","imp",0,0],
  ["STY","zpx",4,0], ["STA","zpx",4,0], ["STX","zpy",4,0], ["???","imp",0,0],
  ["TYA","imp",2,0], ["STA","aby",5,0], ["TXS","imp",2,0], ["???","imp",0,0],
  ["???","imp",0,0], ["STA","abx",5,0], ["???","imp",0,0], ["???","imp",0,0],
  ["LDY","imm",2,0], ["LDA","inx",6,0], ["LDX","imm",2,0], ["???","imp",0,0],
  ["LDY","zpg",3,0], ["LDA","zpg",3,0], ["LDX","zpg",3,0], ["???","imp",0,0],
  ["TAY","imp",2,0], ["LDA","imm",2,0], ["TAX","imp",2,0], ["???","imp",0,0],
  ["LDY","abs",4,0], ["LDA","abs",4,0], ["LDX","abs",4,0], ["???","imp",0,0],
  ["BCS","rel",2,2], ["LDA","iny",5,1], ["???","imp",0,0], ["???","imp",0,0],
  ["LDY","zpx",4,0], ["LDA","zpx",4,0], ["LDX","zpy",4,0], ["???","imp",0,0],
  ["CLV","imp",2,0], ["LDA","aby",4,1], ["TSX","imp",2,0], ["???","imp",0,0],
  ["LDY","abx",4,1], ["LDA","abx",4,1], ["LDX","aby",4,1], ["???","imp",0,0],
  ["CPY","imm",2,0], ["CMP","inx",6,0], ["???","imp",0,0], ["???","imp",0,0],
  ["CPY","zpg",3,0], ["CMP","zpg",3,0], ["DEC","zpg",5,0], ["???","imp",0,0],
  ["INY","imp",2,0], ["CMP","imm",2,0], ["DEX","imp",2,0], ["???","imp",0,0],
  ["CPY","abs",4,0], ["CMP","abs",4,0], ["DEC","abs",3,0], ["???","imp",0,0],
  ["BNE","rel",2,2], ["CMP","iny",5,1], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["CMP","zpx",4,0], ["DEC","zpx",6,0], ["???","imp",0,0],
  ["CLD","imp",2,0], ["CMP","aby",4,1], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["CMP","abx",4,1], ["DEC","abx",7,0], ["???","imp",0,0],
  ["CPX","imm",2,0], ["SBC","inx",6,0], ["???","imp",0,0], ["???","imp",0,0],
  ["CPX","zpg",3,0], ["SBC","zpg",3,0], ["INC","zpg",5,0], ["???","imp",0,0],
  ["INX","imp",2,0], ["SBC","imm",2,0], ["NOP","imp",2,0], ["???","imp",0,0],
  ["CPX","abs",4,0], ["SBC","abs",4,0], ["INC","abs",6,0], ["???","imp",0,0],
  ["BEQ","rel",2,2], ["SBC","iny",5,1], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["SBC","zpx",4,0], ["INC","zpx",6,0], ["???","imp",0,0],
  ["SED","imp",2,0], ["SBC","aby",4,1], ["???","imp",0,0], ["???","imp",0,0],
  ["???","imp",0,0], ["SBC","abx",4,1], ["INC","abx",7,0], ["???","imp",0,0],
];
export { OPS };

const N = 128, V = 64, U = 32, B = 16, D = 8, I = 4, Z = 2, C = 1;
const MODE_SIZE = { imm: 1, zpg: 1, zpx: 1, zpy: 1, inx: 1, iny: 1, abs: 2, abx: 2, aby: 2 };

class Cpu {
  constructor(nes) {
    this.nes = nes;
    this.processorCycles = 0;
    this.excycles = 0;
    this.addcycles = 0;
    this.reset();
    this.buildOps();
  }
  buildOps() {
    this.modes = {};
    for (const m of ["imm", "zpg", "zpx", "zpy", "inx", "iny", "abs", "abx", "aby"]) this.modes[m] = this.modeFn(m);
    this.opFn = OPS.map(([name, mode]) => this.compile(name, mode));
    this.opCyc = Int32Array.from(OPS, o => o[2]);
    this.opExtra = Int32Array.from(OPS, o => o[3]);
  }
  read(a) {
    // Fast paths for RAM and PRG; everything else goes through the bus.
    const n = this.nes;
    if (a < 0x2000) return n.ram[a & 0x7FF];
    if (a >= 0xC000) return n.prgHi[a - 0xC000];
    if (a >= 0x8000) return n.prgLo[a - 0x8000];
    return n.read(a);
  }
  write(a, v) { this.nes.write(a, v); }
  wordAt(a) { return this.read(a) + (this.read(a + 1) << 8); }
  wrapAt(a) { return this.read(a) + (this.read((a & 0xFF00) + ((a + 1) & 0xFF)) << 8); }
  reset() {
    this.pc = this.wordAt(0xFFFC);
    this.sp = 0xFF; this.a = 0; this.x = 0; this.y = 0;
    this.p = B | U;
    this.processorCycles = 0;
  }
  push(z) { this.write(this.sp + 0x100, z & 0xFF); this.sp = (this.sp - 1) & 0xFF; }
  pop() { this.sp = (this.sp + 1) & 0xFF; return this.read(this.sp + 0x100); }
  pushWord(z) { this.push((z >> 8) & 0xFF); this.push(z & 0xFF); }
  popWord() { const lo = this.pop(); return lo + (this.pop() << 8); }
  nz(v) { this.p &= ~(Z | N); if (v === 0) this.p |= Z; else this.p |= v & N; }
  nmi() {
    this.pushWord(this.pc);
    this.p &= ~B;
    this.push(this.p | U);
    this.p |= I;
    this.pc = this.wordAt(0xFFFA);
    this.processorCycles += 7;
  }
  // Effective address per py65 addressing-mode helpers (same reads, same order).
  modeFn(mode) {
    switch (mode) {
      case "imm": return () => this.pc;
      case "zpg": return () => this.read(this.pc);
      case "zpx": return () => (this.x + this.read(this.pc)) & 0xFF;
      case "zpy": return () => (this.y + this.read(this.pc)) & 0xFF;
      case "inx": return () => this.wrapAt((this.read(this.pc) + this.x) & 0xFF);
      case "iny": return () => {
        const a1 = this.wrapAt(this.read(this.pc)), a2 = (a1 + this.y) & 0xFFFF;
        if (this.addcycles && (a1 & 0xFF00) !== (a2 & 0xFF00)) this.excycles += 1;
        return a2;
      };
      case "abs": return () => this.wordAt(this.pc);
      case "abx": return () => {
        const a1 = this.wordAt(this.pc), a2 = (a1 + this.x) & 0xFFFF;
        if (this.addcycles && (a1 & 0xFF00) !== (a2 & 0xFF00)) this.excycles += 1;
        return a2;
      };
      case "aby": return () => {
        const a1 = this.wordAt(this.pc), a2 = (a1 + this.y) & 0xFFFF;
        if (this.addcycles && (a1 & 0xFF00) !== (a2 & 0xFF00)) this.excycles += 1;
        return a2;
      };
    }
    throw new Error("mode " + mode);
  }
  branch() {
    this.excycles += 1;
    const off = this.read(this.pc);
    this.pc += 1;
    const target = (off & N) ? this.pc - (off ^ 0xFF) - 1 : this.pc + off;
    if ((this.pc & 0xFF00) !== (target & 0xFF00)) this.excycles += 1;
    this.pc = target & 0xFFFF;
  }
  compile(name, mode) {
    const size = MODE_SIZE[mode] || 0;
    const ea = this.modes[mode];
    const done = () => { this.pc += size; };
    const rmw = (fn) => () => {
      if (mode === "acc") { this.a = fn(this.a); return; }
      const a = ea();
      const v = fn(this.read(a));
      this.write(a, v);
      done();
    };
    const load = (fn) => () => { fn(this.read(ea())); done(); };
    const store = (reg) => () => { this.write(ea(), this[reg]); done(); };
    const cmp = (reg) => () => {
      const r = this[reg], t = this.read(ea());
      this.p &= ~(C | Z | N);
      if (r === t) this.p |= C | Z; else if (r > t) this.p |= C;
      this.p |= (r - t) & N;
      done();
    };
    const branchIf = (flag, set) => () => {
      if (((this.p & flag) !== 0) === set) this.branch(); else this.pc += 1;
    };
    switch (name) {
      case "???": return () => { this.pc += 1; };
      case "NOP": return () => {};
      case "BRK": return () => {
        this.pushWord((this.pc + 1) & 0xFFFF);
        this.p |= B;
        this.push(this.p | B | U);
        this.p |= I;
        this.pc = this.wordAt(0xFFFE);
      };
      case "ORA": return load(v => { this.a |= v; this.nz(this.a); });
      case "AND": return load(v => { this.a &= v; this.nz(this.a); });
      case "EOR": return load(v => { this.a ^= v; this.nz(this.a); });
      case "LDA": return load(v => { this.a = v; this.nz(v); });
      case "LDX": return load(v => { this.x = v; this.nz(v); });
      case "LDY": return load(v => { this.y = v; this.nz(v); });
      case "BIT": return load(t => {
        this.p &= ~(Z | N | V);
        if ((this.a & t) === 0) this.p |= Z;
        this.p |= t & (N | V);
      });
      case "ADC": return load(data => this.adc(data));
      case "SBC": return load(data => this.sbc(data));
      case "CMP": return cmp("a");
      case "CPX": return cmp("x");
      case "CPY": return cmp("y");
      case "STA": return store("a");
      case "STX": return store("x");
      case "STY": return store("y");
      case "ASL": return rmw(t => {
        this.p &= ~(C | N | Z);
        if (t & N) this.p |= C;
        t = (t << 1) & 0xFF;
        if (t) this.p |= t & N; else this.p |= Z;
        return t;
      });
      case "LSR": return rmw(t => {
        this.p &= ~(C | N | Z);
        this.p |= t & 1;
        t >>= 1;
        if (!t) this.p |= Z;
        return t;
      });
      case "ROL": return rmw(t => {
        if (this.p & C) { if (!(t & N)) this.p &= ~C; t = (t << 1) | 1; }
        else { if (t & N) this.p |= C; t = t << 1; }
        t &= 0xFF;
        this.nz(t);
        return t;
      });
      case "ROR": return rmw(t => {
        if (this.p & C) { if (!(t & 1)) this.p &= ~C; t = (t >> 1) | N; }
        else { if (t & 1) this.p |= C; t = t >> 1; }
        this.nz(t);
        return t;
      });
      case "INC": return rmw(t => {
        this.p &= ~(Z | N);
        t = (t + 1) & 0xFF;
        if (t) this.p |= t & N; else this.p |= Z;
        return t;
      });
      case "DEC": return rmw(t => {
        this.p &= ~(Z | N);
        t = (t - 1) & 0xFF;
        if (t) this.p |= t & N; else this.p |= Z;
        return t;
      });
      case "BPL": return branchIf(N, false);
      case "BMI": return branchIf(N, true);
      case "BVC": return branchIf(V, false);
      case "BVS": return branchIf(V, true);
      case "BCC": return branchIf(C, false);
      case "BCS": return branchIf(C, true);
      case "BNE": return branchIf(Z, false);
      case "BEQ": return branchIf(Z, true);
      case "CLC": return () => { this.p &= ~C; };
      case "SEC": return () => { this.p |= C; };
      case "CLI": return () => { this.p &= ~I; };
      case "SEI": return () => { this.p |= I; };
      case "CLV": return () => { this.p &= ~V; };
      case "CLD": return () => { this.p &= ~D; };
      case "SED": return () => { this.p |= D; };
      case "TAX": return () => { this.x = this.a; this.nz(this.x); };
      case "TAY": return () => { this.y = this.a; this.nz(this.y); };
      case "TXA": return () => { this.a = this.x; this.nz(this.a); };
      case "TYA": return () => { this.a = this.y; this.nz(this.a); };
      case "TSX": return () => { this.x = this.sp; this.nz(this.x); };
      case "TXS": return () => { this.sp = this.x; };
      case "INX": return () => { this.x = (this.x + 1) & 0xFF; this.nz(this.x); };
      case "INY": return () => { this.y = (this.y + 1) & 0xFF; this.nz(this.y); };
      case "DEX": return () => { this.x = (this.x - 1) & 0xFF; this.nz(this.x); };
      case "DEY": return () => { this.y = (this.y - 1) & 0xFF; this.nz(this.y); };
      case "PHA": return () => { this.push(this.a); };
      case "PHP": return () => { this.push(this.p | B | U); };
      case "PLA": return () => { this.a = this.pop(); this.nz(this.a); };
      case "PLP": return () => { this.p = this.pop() | B | U; };
      case "JMP": return mode === "abs"
        ? () => {
          const target = this.wordAt(this.pc);
          if (target === ((this.pc - 1) & 0xFFFF)) this.idle = true;  // jump to itself
          this.pc = target;
        }
        : () => { const ta = this.wordAt(this.pc); this.pc = this.wrapAt(ta); };
      case "JSR": return () => { this.pushWord((this.pc + 1) & 0xFFFF); this.pc = this.wordAt(this.pc); };
      case "RTS": return () => { this.pc = this.popWord(); this.pc += 1; };
      case "RTI": return () => { this.p = this.pop() | B | U; this.pc = this.popWord(); };
    }
    throw new Error("unknown op " + name);
  }
  adc(data) {
    if (this.p & D) {
      let halfcarry = 0, decimalcarry = 0, adjust0 = 0, adjust1 = 0;
      let nibble0 = (data & 0xf) + (this.a & 0xf) + (this.p & C);
      if (nibble0 > 9) { adjust0 = 6; halfcarry = 1; }
      let nibble1 = ((data >> 4) & 0xf) + ((this.a >> 4) & 0xf) + halfcarry;
      if (nibble1 > 9) { adjust1 = 6; decimalcarry = 1; }
      nibble0 &= 0xf; nibble1 &= 0xf;
      const alu = (nibble1 << 4) + nibble0;
      nibble0 = (nibble0 + adjust0) & 0xf;
      nibble1 = (nibble1 + adjust1) & 0xf;
      this.p &= ~(C | V | N | Z);
      if (alu === 0) this.p |= Z; else this.p |= alu & N;
      if (decimalcarry === 1) this.p |= C;
      if ((~(this.a ^ data) & (this.a ^ alu)) & N) this.p |= V;
      this.a = (nibble1 << 4) + nibble0;
      return;
    }
    const result = data + this.a + ((this.p & C) ? 1 : 0);
    this.p &= ~(C | V | N | Z);
    if ((~(this.a ^ data) & (this.a ^ result)) & N) this.p |= V;
    let d = result;
    if (d > 0xFF) { this.p |= C; d &= 0xFF; }
    if (d === 0) this.p |= Z; else this.p |= d & N;
    this.a = d;
  }
  sbc(data) {
    if (this.p & D) {
      let halfcarry = 1, decimalcarry = 0, adjust0 = 0, adjust1 = 0;
      const nibble0in = (this.a & 0xf) + (~data & 0xf) + (this.p & C);
      if (nibble0in <= 0xf) { halfcarry = 0; adjust0 = 10; }
      const nibble1in = ((this.a >> 4) & 0xf) + ((~data >> 4) & 0xf) + halfcarry;
      if (nibble1in <= 0xf) adjust1 = 10 << 4;
      let alu = this.a + (~data & 0xFF) + (this.p & C);
      if (alu > 0xFF) decimalcarry = 1;
      alu &= 0xFF;
      const nibble0 = (alu + adjust0) & 0xf;
      const nibble1 = ((alu + adjust1) >> 4) & 0xf;
      this.p &= ~(C | Z | N | V);
      if (alu === 0) this.p |= Z; else this.p |= alu & N;
      if (decimalcarry === 1) this.p |= C;
      if (((this.a ^ data) & (this.a ^ alu)) & N) this.p |= V;
      this.a = (nibble1 << 4) + nibble0;
      return;
    }
    const result = this.a + (~data & 0xFF) + (this.p & C);
    this.p &= ~(C | Z | V | N);
    if (((this.a ^ data) & (this.a ^ result)) & N) this.p |= V;
    const d = result & 0xFF;
    if (d === 0) this.p |= Z;
    if (result > 0xFF) this.p |= C;
    this.p |= d & N;
    this.a = d;
  }
  step() {
    const code = this.read(this.pc);
    this.pc = (this.pc + 1) & 0xFFFF;
    this.excycles = 0;
    this.addcycles = this.opExtra[code];
    this.opFn[code]();
    this.pc &= 0xFFFF;
    this.processorCycles += this.opCyc[code] + this.excycles;
  }
}

export class Nes {
  constructor(ines) {
    if (ines[0] !== 0x4E || ines[1] !== 0x45 || ines[2] !== 0x53 || ines[3] !== 0x1A) throw new Error("not an iNES image");
    if (((ines[6] >> 4) | (ines[7] & 0xF0)) !== 1 || ines[5] !== 0) throw new Error("expected mapper 1 with CHR-RAM");
    this.frame = 0;
    this.prg = [];
    for (let i = 0; i < ines[4]; i++) this.prg.push(ines.subarray(16 + i * 0x4000, 16 + (i + 1) * 0x4000));
    this.ram = new Uint8Array(0x800);
    this.wram = new Uint8Array(0x2000);
    this.chr = new Uint8Array(0x2000);
    this.ciram = new Uint8Array(0x800);
    this.palram = new Uint8Array(0x20);
    this.oam = new Uint8Array(0x100);
    this.oamAddr = 0;
    this.mmc1Shift = 0x10; this.mmc1Control = 0x0C; this.mmc1Prg = 0; this.mmc1Chr0 = 0; this.mmc1Chr1 = 0;
    this.mapPrg();
    this.ppuCtrl = 0; this.ppuMask = 0; this.vblank = false; this.sprite0 = false;
    this.v = 0; this.t = 0; this.w = 0; this.readBuf = 0;
    this.padState = 0; this.padShift = 0; this.padStrobe = 0;
    this.cpu = new Cpu(this);   // py65 MPU(...) resets in __init__ ...
    this.cpu.reset();           // ... and nes.py calls reset() again
    this.frame = 0;
    this.frameStart = 0;
  }
  mapPrg() {
    const mode = (this.mmc1Control >> 2) & 3, bank = this.mmc1Prg & 0x0F, last = this.prg.length - 1;
    if (mode < 2) { const b = bank & ~1; this.prgLo = this.prg[b]; this.prgHi = this.prg[b + 1]; }
    else if (mode === 2) { this.prgLo = this.prg[0]; this.prgHi = this.prg[bank]; }
    else { this.prgLo = this.prg[bank]; this.prgHi = this.prg[last]; }
  }
  mmc1Write(addr, value) {
    if (value & 0x80) { this.mmc1Shift = 0x10; this.mmc1Control |= 0x0C; this.mapPrg(); return; }
    const full = this.mmc1Shift & 1;
    this.mmc1Shift = (this.mmc1Shift >> 1) | ((value & 1) << 4);
    if (!full) return;
    const data = this.mmc1Shift;
    this.mmc1Shift = 0x10;
    const reg = (addr >> 13) & 3;
    if (reg === 0) this.mmc1Control = data;
    else if (reg === 1) this.mmc1Chr0 = data;
    else if (reg === 2) this.mmc1Chr1 = data;
    else this.mmc1Prg = data;
    this.mapPrg();
  }
  read(addr) {
    if (addr < 0x2000) return this.ram[addr & 0x7FF];
    if (addr >= 0x8000) return addr >= 0xC000 ? this.prgHi[addr - 0xC000] : this.prgLo[addr - 0x8000];
    if (addr >= 0x6000) return this.wram[addr - 0x6000];
    if (addr < 0x4000) return this.ppuRead(addr & 7);
    if (addr === 0x4016) return this.readPad();
    return 0;
  }
  write(addr, value) {
    if (addr < 0x2000) this.ram[addr & 0x7FF] = value;
    else if (addr >= 0x8000) this.mmc1Write(addr, value);
    else if (addr >= 0x6000) this.wram[addr - 0x6000] = value;
    else if (addr < 0x4000) this.ppuWrite(addr & 7, value);
    else if (addr === 0x4014) {
      const page = value << 8, at = this.oamAddr;
      for (let i = 0; i < 256; i++) this.oam[(at + i) & 0xFF] = this.read(page + i);
      this.cpu.processorCycles += 513;
    } else if (addr === 0x4016) {
      this.padStrobe = value & 1;
      if (this.padStrobe) this.padShift = this.padState;
    }
  }
  ntIndex(a) {
    a &= 0x0FFF;
    const m = this.mmc1Control & 3;
    if (m === 0) return a & 0x3FF;
    if (m === 1) return 0x400 | (a & 0x3FF);
    if (m === 2) return a & 0x7FF;
    return ((a >> 1) & 0x400) | (a & 0x3FF);
  }
  palIndex(a) { a &= 0x1F; if ((a & 0x13) === 0x10) a &= 0x0F; return a; }
  ppuBusWrite(a, value) {
    a &= 0x3FFF;
    if (a < 0x2000) this.chr[a] = value;
    else if (a < 0x3F00) this.ciram[this.ntIndex(a)] = value;
    else this.palram[this.palIndex(a)] = value & 0x3F;
  }
  ppuBusRead(a) {
    a &= 0x3FFF;
    if (a < 0x2000) return this.chr[a];
    if (a < 0x3F00) return this.ciram[this.ntIndex(a)];
    return this.palram[this.palIndex(a)];
  }
  ppuRead(reg) {
    if (reg === 2) {
      const c = this.cpu.processorCycles - this.frameStart;
      const vb = this.vblank && c < VBLANK_CYCLES;
      const s0 = (c >= VBLANK_CYCLES + SPRITE0_LINE * CYCLES_PER_LINE) || (this.sprite0 && c < VBLANK_CYCLES);
      this.vblank = false;
      this.w = 0;
      return (vb ? 0x80 : 0) | (s0 ? 0x40 : 0);
    }
    if (reg === 4) return this.oam[this.oamAddr];
    if (reg === 7) {
      const a = this.v & 0x3FFF;
      let value;
      if (a >= 0x3F00) { value = this.ppuBusRead(a); this.readBuf = this.ppuBusRead(a - 0x1000); }
      else { value = this.readBuf; this.readBuf = this.ppuBusRead(a); }
      this.v = (this.v + ((this.ppuCtrl & 4) ? 32 : 1)) & 0x7FFF;
      return value;
    }
    return 0;
  }
  ppuWrite(reg, value) {
    if (reg === 0) { this.ppuCtrl = value; this.t = (this.t & 0x73FF) | ((value & 3) << 10); }
    else if (reg === 1) this.ppuMask = value;
    else if (reg === 3) this.oamAddr = value;
    else if (reg === 4) { this.oam[this.oamAddr] = value; this.oamAddr = (this.oamAddr + 1) & 0xFF; }
    else if (reg === 5) {
      if (this.w === 0) this.t = (this.t & 0x7FE0) | (value >> 3);
      else this.t = (this.t & 0x0C1F) | ((value & 7) << 12) | ((value & 0xF8) << 2);
      this.w ^= 1;
    } else if (reg === 6) {
      if (this.w === 0) this.t = (this.t & 0x00FF) | ((value & 0x3F) << 8);
      else { this.t = (this.t & 0x7F00) | value; this.v = this.t; }
      this.w ^= 1;
    } else if (reg === 7) {
      this.ppuBusWrite(this.v, value);
      this.v = (this.v + ((this.ppuCtrl & 4) ? 32 : 1)) & 0x7FFF;
    }
  }
  setButtons(...names) {
    let state = 0;
    for (const n of names) state |= 1 << BUTTONS.indexOf(n);
    this.padState = state;
  }
  readPad() {
    if (this.padStrobe) return 0x40 | (this.padState & 1);
    const bit = this.padShift & 1;
    this.padShift = (this.padShift >> 1) | 0x80;
    return 0x40 | bit;
  }
  runFrame() {
    const cpu = this.cpu;
    this.frameStart = cpu.processorCycles;
    this.vblank = true;
    this.sprite0 = true;
    if (this.ppuCtrl & 0x80) cpu.nmi();
    const end = this.frameStart + CYCLES_PER_FRAME;
    cpu.idle = false;
    while (cpu.processorCycles < end) {
      cpu.step();
      // Idle skip: a `JMP` to itself (Zelda's wait-for-NMI loop) only re-reads
      // its own operand, so finish py65's remaining 3-cycle iterations at once.
      if (cpu.idle) {
        cpu.idle = false;
        if (cpu.pc >= 0x8000 && cpu.processorCycles < end) {
          cpu.processorCycles += 3 * Math.ceil((end - cpu.processorCycles) / 3);
        }
      }
    }
    this.frame += 1;
  }
  clone() {
    const n = Object.create(Nes.prototype);
    Object.assign(n, this);
    for (const k of ["ram", "wram", "chr", "ciram", "palram", "oam"]) n[k] = this[k].slice();
    n.cpu = Object.create(Object.getPrototypeOf(this.cpu));
    Object.assign(n.cpu, this.cpu);
    n.cpu.nes = n;
    n.cpu.buildOps();
    return n;
  }
  bus(addr) { return this.read(addr); }
  poke(addr, value) { this.write(addr, value & 0xFF); }
}

/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "c1/c1_Runtime1.hpp"
#include "classfile/systemDictionary.hpp"
#include "gc_interface/collectedHeap.hpp"
#include "interpreter/interpreter.hpp"
#include "oops/arrayOop.hpp"
#include "oops/markOop.hpp"
#include "runtime/basicLock.hpp"
#include "runtime/biasedLocking.hpp"
#include "runtime/os.hpp"
#include "runtime/stubRoutines.hpp"

int C1_MacroAssembler::lock_object(Register hdr, Register obj, Register disp_hdr, Register scratch, Label& slow_case) { Unimplemented(); return 0; }


void C1_MacroAssembler::unlock_object(Register hdr, Register obj, Register disp_hdr, Label& slow_case) { call_Unimplemented(); }


// Defines obj, preserves var_size_in_bytes
void C1_MacroAssembler::try_allocate(Register obj, Register var_size_in_bytes, int con_size_in_bytes, Register t1, Register t2, Label& slow_case) {
  if (UseTLAB) {
    tlab_allocate(obj, var_size_in_bytes, con_size_in_bytes, t1, t2, slow_case);
  } else {
    ShouldNotReachHere();
    // eden_allocate(obj, var_size_in_bytes, con_size_in_bytes, t1, slow_case);
    // incr_allocated_bytes(noreg, var_size_in_bytes, con_size_in_bytes, t1);
  }
}

void C1_MacroAssembler::initialize_header(Register obj, Register klass, Register len, Register t1, Register t2) {
  assert_different_registers(obj, klass, len);
  if (UseBiasedLocking && !len->is_valid()) {
    assert_different_registers(obj, klass, len, t1, t2);
    ldr(t1, Address(klass, Klass::prototype_header_offset()));
  } else {
    // This assumes that all prototype bits fit in an int32_t
    mov(t1, (int32_t)(intptr_t)markOopDesc::prototype());
  }
  str(t1, Address(obj, oopDesc::mark_offset_in_bytes()));

  if (UseCompressedKlassPointers) { // Take care not to kill klass
    encode_heap_oop_not_null(t1, klass);
    strw(t1, Address(obj, oopDesc::klass_offset_in_bytes()));
  } else {
    str(klass, Address(obj, oopDesc::klass_offset_in_bytes()));
  }

  if (len->is_valid()) {
    strw(len, Address(obj, arrayOopDesc::length_offset_in_bytes()));
  } else if (UseCompressedKlassPointers) {
    store_klass_gap(obj, zr);
  }
}


// preserves obj, destroys len_in_bytes
void C1_MacroAssembler::initialize_body(Register obj, Register len_in_bytes, int hdr_size_in_bytes, Register t1) {
  Label done;
  assert(obj != len_in_bytes && obj != t1 && t1 != len_in_bytes, "registers must be different");
  assert((hdr_size_in_bytes & (BytesPerWord - 1)) == 0, "header size is not a multiple of BytesPerWord");
  Register index = len_in_bytes;
  // index is positive and ptr sized
  subs(index, index, hdr_size_in_bytes);
  br(Assembler::EQ, done);
  // note: for the remaining code to work, index must be a multiple of BytesPerWord
#ifdef ASSERT
  { Label L;
    tst(index, BytesPerWord - 1);
    br(Assembler::EQ, L);
    stop("index is not a multiple of BytesPerWord");
    bind(L);
  }
#endif

  lsr(index, index, LogBytesPerWord);
  add(rscratch1, obj, hdr_size_in_bytes);
  { Label loop;
    bind(loop);
    str(zr, Address(post(rscratch1, BytesPerWord)));
    sub(index, index, 1);
    cbnz(index, loop);
  }

  // done
  bind(done);
}


void C1_MacroAssembler::allocate_object(Register obj, Register t1, Register t2, int header_size, int object_size, Register klass, Label& slow_case) { Unimplemented(); }

void C1_MacroAssembler::initialize_object(Register obj, Register klass, Register var_size_in_bytes, int con_size_in_bytes, Register t1, Register t2) { Unimplemented(); }

void C1_MacroAssembler::allocate_array(Register obj, Register len, Register t1, Register t2, int header_size, int f, Register klass, Label& slow_case) {
  assert_different_registers(obj, len, t1, t2, klass);

  // determine alignment mask
  assert(!(BytesPerWord & 1), "must be a multiple of 2 for masking code to work");

  // check for negative or excessive length
  mov(rscratch1, (int32_t)max_array_allocation_length);
  cmp(len, rscratch1);
  br(Assembler::HS, slow_case);

  const Register arr_size = t2; // okay to be the same
  // align object end
  mov(arr_size, (int32_t)header_size * BytesPerWord + MinObjAlignmentInBytesMask);
  add(arr_size, arr_size, len, ext::uxtw, f);
  andr(arr_size, arr_size, ~MinObjAlignmentInBytesMask);

  try_allocate(obj, arr_size, 0, t1, t2, slow_case);

  initialize_header(obj, klass, len, t1, t2);

  // clear rest of allocated space
  const Register len_zero = len;
  initialize_body(obj, arr_size, header_size * BytesPerWord, len_zero);

  if (CURRENT_ENV->dtrace_alloc_probes()) {
    assert(obj == r0, "must be");
    bl(RuntimeAddress(Runtime1::entry_for(Runtime1::dtrace_object_alloc_id)));
  }

  verify_oop(obj);
}


void C1_MacroAssembler::inline_cache_check(Register receiver, Register iCache) { Unimplemented(); }


void C1_MacroAssembler::build_frame(int frame_size_in_bytes) {
  // Make sure there is enough stack space for this method's activation.
  // Note that we do this before doing an enter().
  generate_stack_overflow_check(frame_size_in_bytes);
  enter();
  sub(sp, sp, frame_size_in_bytes); // does not emit code for frame_size == 0
}


void C1_MacroAssembler::remove_frame(int frame_size_in_bytes) {
  leave();
}


void C1_MacroAssembler::unverified_entry(Register receiver, Register ic_klass) { Unimplemented(); }


void C1_MacroAssembler::verified_entry() {
}


#ifndef PRODUCT

void C1_MacroAssembler::verify_stack_oop(int stack_offset) { Unimplemented(); }

void C1_MacroAssembler::verify_not_null_oop(Register r) {
  if (!VerifyOops) return;
  Label not_null;
  cbnz(r, not_null);
  stop("non-null oop required");
  bind(not_null);
  verify_oop(r);
}

void C1_MacroAssembler::invalidate_registers(bool inv_rax, bool inv_rbx, bool inv_rcx, bool inv_rdx, bool inv_rsi, bool inv_rdi) {
#ifdef ASSERT
  if (inv_rax) mov(r0, 0xDEAD);
  if (inv_rbx) mov(r19, 0xDEAD);
  if (inv_rcx) mov(r2, 0xDEAD);
  if (inv_rdx) mov(r3, 0xDEAD);
  if (inv_rsi) mov(r4, 0xDEAD);
  if (inv_rdi) mov(r5, 0xDEAD);
#endif
}
#endif // ifndef PRODUCT
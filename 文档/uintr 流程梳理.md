基于 <https://github.com/OS-F-4/uintr-linux-kernel/commits/rfc-v1> 版本,梳理 用户中断从发送到接收的流程, 包含硬件执行流程(这里对应 qemu).

- qemu 的实现 <https://github.com/OS-F-4/qemu-uintr>

uintr_receiver_wait 流程梳理,已有问题等.

资料:

- <https://www.intel.com/content/dam/develop/external/us/en/documents/architecture-instruction-set-extensions-programming-reference.pdf>
- <https://github.com/OS-F-4/usr-intr/tree/main/ppt>
- <https://www.0xaa55.com/thread-27327-1-1.html>

## 起

由 sample.c (位于 kernel 代码的 `tools/uintr/sample` ) 开始

```c
xxx
void __attribute__ ((interrupt)) uintr_handler(struct __uintr_frame *ui_frame,unsigned long long vector){//用户态中断处理函数, 功能为将uintr_received置1
	uintr_received = 1;
}
xxx
void *sender_thread(void *arg){ // 发送方线程执行的函数
	int uipi_index = uintr_register_sender(uintr_fd, 0);// 注册sender
	_senduipi(uipi_index); // 通过senduipi发送中断
	uintr_unregister_sender(uintr_fd, 0);// 注销sender
	return NULL;
}
xxx
```

发送方注册后,发送的起点是 `_senduipi(uipi_index);`

## 发送

send 执行 `_senduipi(uipi_index);` 定义在 `gcc/x86_64-linux-gnu/11/include/uintrintrin.h`

```c
_senduipi (unsigned long long __R)
{
  __builtin_ia32_senduipi (__R);
}
```

具体是 [Enable gcc support for UINTR](https://github.com/gcc-mirror/gcc/commit/299a53d7979aaa639298b95bd46b69d3a8546f49) 这个 commit 给 gcc 增加的 uintr 支持.

```c
BDESC (OPTION_MASK_ISA_64BIT, OPTION_MASK_ISA2_UINTR, CODE_FOR_senduipi, "__builtin_ia32_senduipi", IX86_BUILTIN_SENDUIPI, UNKNOWN, (int) VOID_FTYPE_UINT64)


(define_insn "senduipi"
  [(unspec_volatile
    [(match_operand:DI 0 "register_operand" "r")]
    UNSPECV_SENDUIPI)]
  "TARGET_UINTR && TARGET_64BIT"
  "senduipi\t%0"
  [(set_attr "type" "other")
   (set_attr "length" "4")])
```

`_senduipi` 指令在用户态完成; 硬件处理流程定义在 [[architecture-instruction-set-extensions-programming-reference.pdf#page=75]]

```c
# reg == uipi_index
IF reg > UITTSZ;  # reg 大于 UITTSZ
    THEN #GP(0);  
FI;

read tempUITTE from 16 bytes at UITTADDR+ (reg « 4);  # 从 uitt_ctx->uitt 中读取对应的 UITTE(固定16字节)
// [[architecture-instruction-set-extensions-programming-reference.pdf#page=169|USER IPIS]], UITTE 的第一位是否为 1,保留位置是否被使用
IF tempUITTE.V = 0 or tempUITTE sets any reserved bit (see Section 11.7.1)  
    THEN #GP(0);  
FI;  

read tempUPID from 16 bytes at tempUITTE.UPIDADDR;// under lock | 从 uintr_uitt_entry->target_upid_addr 读取 UPID
IF tempUPID sets any reserved bits or bits that must be zero (see Table 11-1)  // 检查保留位
    THEN #GP(0); // release lock  
FI;  
tempUPID.PIR[tempUITTE.UV] := 1; # uintr_upid->puir[uv] = 1 置为相应中断号位置
IF tempUPID.SN = tempUPID.ON = 0 # uintr_upid 的 UPID_ON 和 UPID_SN 都为 0 才能正常发送
    THEN  
      tempUPID.ON := 1;  # set UPID_ON = 1
      sendNotify := 1;  # sendNotify = 1
    LSE sendNotify := 0;  # 否则 sendNotify = 0
FI;  
write tempUPID to 16 bytes at tempUITTE.UPIDADDR;// release lock | 写回 UPID 释放锁
IF sendNotify = 1  # 如果 upid 的 UPID_SN = 1 发送 tempUPID.NV(对应 uintr_upid->nc->nv)
    THEN  
        IF local APIC is in x2APIC mode
            THEN send ordinary IPI with vector tempUPID.NV  # apic 处于 x2APIC 模式
                to 32-bit physical APIC ID tempUPID.NDST;  
            ELSE send ordinary IPI with vector tempUPID.NV  # apic 处于 xAPIC 模式
                to 8-bit physical APIC ID tempUPID.NDST[15:8];  
        FI;  
FI;
```

^803efc

发送方发送的条件

- 用户中断启用 UIF
- UPID_ON 和 UPID_SN 都为 0
- 发送用户中断后,置位 UPID_ON

发送方并没有对发送的 `tempUPID.NV` 中断向量进行比较或操作.

---

对应到 qemu 的 target/i386/tcg/misc_helper.c 的 `void helper_senduipi(CPUX86State *env ,int reg_index)`

- 疑似有一个 bug `upid.nc.status&0x03` 比较对象应该是 0x03 而不是 0x11

```c
void helper_senduipi(CPUX86State *env ,int reg_index){
    uint32_t uittsz = (uint32_t)env->uintr_misc;
    int uitte_index = env->regs[R_EAX];
    if(reg_index == 244){
        uitte_index = env->regs[R_R12];
        // qemu_log("read from r12, index :%d\n", uitte_index);
    }
    if (uitte_index > uittsz){
        raise_exception_ra(env, EXCP0D_GPF, GETPC());
    }
    qemu_log("uitte index:%d\n", uitte_index);

    CPUState *cs = env_cpu(env);

    // read tempUITTE from 16 bytes at UITTADDR+ (reg « 4);
    uint64_t uitt_phyaddress = get_hphys2(cs, (env->uintr_tt>>3)<<3 , MMU_DATA_LOAD, NULL);
    struct uintr_uitt_entry uitte;
    cpu_physical_memory_rw(uitt_phyaddress + (uitte_index<<4), &uitte, 16,false);

    // read tempUPID from 16 bytes at tempUITTE.UPIDADDR;// under lock
    qemu_mutex_lock_iothread();
    uint64_t upid_phyaddress = get_hphys2(cs, uitte.target_upid_addr, MMU_DATA_LOAD, NULL);
    qemu_log("uitt addr: 0x%lx  upid addr: 0x%lx\n", env->uintr_tt, uitte.target_upid_addr); 
    struct uintr_upid upid;
    cpu_physical_memory_rw(upid_phyaddress, &upid, 16, false);
    // tempUPID.PIR[tempUITTE.UV] := 1;
    upid.puir |= 1<<uitte.user_vec;
    bool sendNotify;
    // IF tempUPID.SN = tempUPID.ON = 0
    // 03 比较 ON 和 SN, 原来这里是 11 疑似 bug.
    if((upid.nc.status&0x03) == 0){
    //THEN  tempUPID.ON := 1;   sendNotify := 1;
        upid.nc.status |= UPID_ON;
        sendNotify = true;
    }else{ // ELSE sendNotify := 0;
        sendNotify = false;
    }
    //write tempUPID to 16 bytes at tempUITTE.UPIDADDR;// release lock
    cpu_physical_memory_rw(upid_phyaddress, &upid, 16, true);
    qemu_mutex_unlock_iothread();

    if(sendNotify){
        qemu_log("senduipi for real \n");
        uint8_t realdst = upid.nc.ndst >> 8;
        send_ipi(realdst, upid.nc.nv);
    }
}
```

---

## 接收

[[architecture-instruction-set-extensions-programming-reference.pdf#page=167|11.5.1 User-Interrupt Notification Identification]] 收到中断通知,开始判断

1. The local APIC is acknowledged; this provides the processor core with an interrupt vector, V.

- 从 APIC 中读取 V

2. If V = UINV, the logical processor continues to the next step. Otherwise, an interrupt with vector V is delivered normally through the IDT; the remainder of this algorithm does not apply and user-interrupt notification processing does not occur.

- V == UINV? 是就是 uintr 了执行第 3 步.否则直接跳转到 IDT 处理,普通中断处理. ^dfc1d3
- linux kernel 中 UINV 是 MSR_IA32_UINTR_MISC 的 39:32;  内核源码中每次都被赋值为 UINTR_NOTIFICATION_VECTOR(0xec) << 32

3. The processor writes zero to the EOI register in the local APIC; this dismisses the interrupt with vector V = UINV from the local APIC.

- 写 EOI 寄存器,清除 V = UINV 的中断

[[architecture-instruction-set-extensions-programming-reference.pdf#page=168|11.5.2 User-Interrupt Notification Identification]] 从 IA32_UINTR_PD 读取 upid,开始处理 uintr.

1. The logical processor clears the outstanding-notification bit (bit 0) in the UPID. This is done atomically so as to leave the remainder of the descriptor unmodified (e.g., with a locked AND operation).

- 清理 UPID 的 ON 位(bit 0),对应 uintr_upid->nc->status首位,并读取 PIR 到临时寄存器,并将 PIR 清零

2. The logical processor reads PIR (bits 127:64 of the UPID) into a temporary register and writes all zeros to PIR. This is done atomically so as to ensure that each bit cleared is set in the temporary register (e.g., with a locked XCHG operation).
3. If any bit is set in the temporary register, the logical processor sets in UIRR each bit corresponding to a bit set in the temporary register (e.g., with an OR operation) and recognizes a pending user interrupt (if it has not already done so).

- 2-3步其实是将 UPID.PIR(uintr_upid->puir) 所有为 1 的位写入 UIRR,之后清空 UPID.PIR(uintr_upid->puir). ^5f2176

步骤 12 会在逻辑处理器上不间断执行,再执行第3步,最终跳跃到 11.4.2 真正的跳转处理中.

[[architecture-instruction-set-extensions-programming-reference.pdf#page=165|11.4.2 User-Interrupt Delivery]] 最终跳转到 UIHANDLER

```python
IF UIHANDLER is not canonical in current paging mode # UIHANDLER(handler 函数地址) 是不是合法
    THEN #GP(0);
FI;
tempRSP := RSP; # 调整 RSP(当前栈顶地址) 切换 or 偏移
IF UISTACKADJUST[0] = 1
    THEN RSP := UISTACKADJUST;
    ELSE RSP := RSP – UISTACKADJUST;
FI;
RSP := RSP & ~FH; // force the stack to be 16-byte aligned
Push tempRSP;
Push RFLAGS;
Push RIP;
Push UIRRV; // 64-bit push; upper 58 bits pushed as 0
IF shadow stack is enabled for CPL = 3
    THEN ShadowStackPush RIP;
FI;
IF end-branch is enabled for CPL = 3
    THEN IA32_U_CET.TRACKER := WAIT_FOR_ENDBRANCH;
FI;
UIRR[Vector] := 0; # UIRR 对应位置 0,处理完这个请求了
IF UIRR = 0 # UIRR 全部处理完了
    THEN cease recognition of any pending user interrupt;# 停止识别任何待处理的用户中断
FI;
UIF := 0; # 清除 UIF
RFLAGS.TF := 0;
RFLAGS.RF := 0;
RIP := UIHANDLER; # 跳转到 handler 函数
```

值得注意的几点:

- 接收时 会比较发送方发送的中断向量 与 UINV (MSR_IA32_UINTR_MISC 的 39:32).相同则认为其是用户中断, 否则就走普通中断处理流程.
- 接收时要求接收方进程处于前台(用户态)
- 最终跳转到 接收方注册的 handler 函数执行用户中断.

---

qemu 中上面整个过程对应代码在 `target/i386/tcg/seg_helper.c` :: `static void do_interrupt64(CPUX86State *env, int intno, int is_int,int error_code, target_ulong next_eip, int is_hw)`

```c
    bool send = false;
    if(intno == UINTR_UINV ){ // V = UINV? 比较
        qemu_log("receive, cur core:%d\n",get_apic_id(cpu_get_current_apic()));
        recognized = true;
        cpl = env->hflags & HF_CPL_MASK;
        if(!uif_enable(env)){
            DeviceState *dev = cpu_get_current_apic();
            int id = get_apic_id(dev);
            qemu_log("--uif zero,prev:%d | id:%d return\n",cpl, id);
            rrzero_count +=1; 
            if(rrzero_count > 2000){
                qemu_log("too many zeros, exit\n");
                exit(2);
            }
            helper_clear_eoi(env);
            return;
        }
        if(cpl != 3){
            //查看当前的权级
            DeviceState *dev = cpu_get_current_apic();
            int id = get_apic_id(dev);
            helper_clear_eoi(env);
            qemu_log("perv: %d | id:%d not in user mode return\n", cpl,id);
            return;
        }
        CPUState *cs = env_cpu(env);
        uint64_t upid_phyaddress = get_hphys2(cs, env->uintr_pd, MMU_DATA_LOAD, NULL);
        uintr_upid upid;
        cpu_physical_memory_rw(upid_phyaddress, &upid, 16, false);
        upid.nc.status &= (~1); // clear on
        if(upid.puir != 0){
            env->uintr_rr = upid.puir;
            upid.puir = 0; // clear puir
            send = true;
        }
        cpu_physical_memory_rw(upid_phyaddress, &upid, 16, true);  // write back
        helper_clear_eoi(env);

        if(send)helper_rrnzero(env);
        else qemu_log("do not go to handler\n");
        return;
    }
```

---

### uintr_receiver_wait 流程梳理

rfc_v1 中添加了 `uintr_receiver_wait`, 使得接收方可阻塞挂起等待用户中断.

为什么 uintr_receiver_wait 以后能够再唤醒线程 ?

uintr_receiver_wait

- upid->nc.nv = UINTR_KERNEL_VECTOR;
- 将发送方 upid 加入一个全局的 upid 等待 list (upid 中包含了进程的 trask_struct)

发送方发送的中断向量 就是 UINTR_KERNEL_VECTOR (0xeb) 而不是 用户中断对应的 UINTR_NOTIFICATION_VECTOR (0xec).

在处理 [[#^dfc1d3|V == UINV? ]] 时候就走到了 IDT 不再是用户中断,而是会 触发内核中断 -> DEFINE_IDTENTRY_SYSVEC(sysvec_uintr_kernel_notification)

sysvec_uintr_kernel_notification 会执行 uintr_wake_up_process(void)

- 遍历 upid 等待 list 查找待 有处理的 upid
- 设置 SN = 1, 启用接收.
- uintr_wake_up_process 唤醒后. upid 等待 list 清除当前 upid.

唤醒之后线程会执行 switch_uintr_return, 在这个函数中恢复上下文,并且再次 `apic->send_IPI_self(UINTR_NOTIFICATION_VECTOR);` 触发进入 uintr 处理流程. ^43673f

uintr_wait 的问题?

- 有概率无法在等待中返回 --> 目前无解,参考这个 [commit](https://github.com/intel/uintr-linux-kernel/commit/b855959c2fe314fca477b19b244acdb876294712). poc_v2 是否解决待验证.
- 没有定时 --> 参考 poc_v2, [已添加定时器](https://git.mesalab.cn/zhangyang/uintr-linux-kernel/-/commit/76b865af2d59d018d8e2805ea9ac7a411361265b),并测试通过.

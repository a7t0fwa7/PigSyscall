#ifndef PIG_SYSCALL_HPP_
#define PIG_SYSCALL_HPP_

#include <intrin.h>
#include <windows.h>
#include <winternl.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "native.hpp"
#include "util.hpp"

//#define PROXY_CALL  //�Ƿ����Զ����ջ����

//���ȥhash���������string
// constexpr uint32_t hash
//using SyscallMap = std::unordered_map<uint32_t, uint32_t>;
//��һ����API Name���ڶ�����ΪSSN
using SyscallMap = std::unordered_map<uint32_t, uint32_t>;   //unordered_mapʹ��hash������Ч�ʿ�

using NtStatus = uint32_t;
extern uint8_t encrypted_manual_syscall_stub[];
extern uint8_t encrypted_masked_syscall_stub[];
extern uint8_t WorkCallback_stub[];


//typedef NTSTATUS(NTAPI* TPALLOCWORK)(PTP_WORK* ptpWrk, PTP_WORK_CALLBACK pfnwkCallback, PVOID OptionalArg, PTP_CALLBACK_ENVIRON CallbackEnvironment);
typedef VOID(NTAPI* TPPOSTWORK)(PTP_WORK);
typedef VOID(NTAPI* TPRELEASEWORK)(PTP_WORK);

namespace pigsyscall {

class syscall {

private:

    static inline SyscallMap syscall_map;

    static void ExtractSSNs() noexcept;
    
    // Private constructor
    syscall() noexcept {
        ExtractSSNs();
    };

    uintptr_t FindSyscallOffset() noexcept;
    
    template<typename... ServiceArgs>
    NtStatus InternalCaller(uint32_t syscall_no, uintptr_t stub_addr, ServiceArgs... args) noexcept {

#ifdef PROXY_CALL

        FARPROC pTpAllocWork = GetProcAddress(GetModuleHandleA("ntdll"), "TpAllocWork");
        FARPROC pTpPostWork = GetProcAddress(GetModuleHandleA("ntdll"), "TpPostWork");
        FARPROC pTpReleaseWork = GetProcAddress(GetModuleHandleA("ntdll"), "TpReleaseWork");

        //std::size_t argsize = sizeof...(ServiceArgs);
        //auto data = std::make_tuple(std::forward<ServiceArgs>(args)...);

        //std::size_t argBufferSize = argsize * sizeof(char*);
        //WorkCallbackArgAddr = (char*)malloc(argBufferSize);
        //memset(WorkCallbackArgAddr, 0x00, argBufferSize);

        //OperateTuple(data);
        typedef NTSTATUS(NTAPI* TPALLOCWORK)(PTP_WORK* ptpWrk, PTP_WORK_CALLBACK pfnwkCallback, uint32_t syscall_no, ServiceArgs... args, PTP_CALLBACK_ENVIRON CallbackEnvironment);

        using StubDef = NtStatus(__stdcall*)(uint32_t, ServiceArgs...);
        
        StubDef stub = reinterpret_cast<decltype(stub)>(&WorkCallback_stub);

        PTP_WORK WorkReturn = NULL;
        ((TPALLOCWORK)pTpAllocWork)(&WorkReturn, (PTP_WORK_CALLBACK)&stub, syscall_no, std::forward<ServiceArgs>(args)..., NULL);
        ((TPPOSTWORK)pTpPostWork)(WorkReturn);
        ((TPRELEASEWORK)pTpReleaseWork)(WorkReturn);

        WaitForSingleObject((HANDLE)-1, 0x1000);

        //free(WorkCallbackArgAddr);

#else
        using StubDef = NtStatus(__stdcall*)(uint32_t, ServiceArgs...);
        StubDef stub = reinterpret_cast<decltype(stub)>(stub_addr);
        //decrypt stub
        //strlen maybe not beauty?
        pigsyscall::utils::CryptPermute((PVOID)stub_addr, strlen((char*)stub_addr), FALSE);
        NtStatus return_value = stub(syscall_no, std::forward<ServiceArgs>(args)...);   //����ת������ָstd::forward�Ὣ����Ĳ���ԭ�ⲻ���ش��ݵ���һ��������

#endif
        return 1;
    }

public:

    // Disable any other constructor or assignment operator
    syscall(const syscall&) = delete;
    syscall& operator=(const syscall&) = delete;
    syscall(syscall&&) = delete;
    syscall& operator=(syscall&&) = delete;

    // Singleton instance getter
    // ����ģʽ����
    static inline syscall& get_instance() noexcept {
        static syscall instance{};
        return instance;
    }

    [[nodiscard]] uint32_t GetSyscallNumber(uint32_t stub_name_hashed);

    template<typename... ServiceArgs>
    NtStatus CallSyscall(uint32_t stub_name_hashed, ServiceArgs... args) {
        uint32_t syscall_no;
        uintptr_t stub_addr;
        uintptr_t syscall_inst_addr;

        syscall_no = GetSyscallNumber(stub_name_hashed);
        syscall_inst_addr = FindSyscallOffset();

        // If the syscall instruction has not been found, use the direct stub. To use the masked stub
        // we need the instruction to be in the original stub.
        if (!syscall_inst_addr) {
            return InternalCaller(syscall_no, reinterpret_cast<uintptr_t>(&encrypted_manual_syscall_stub), std::forward<ServiceArgs>(args)...);
        }

        return InternalCaller(syscall_no, reinterpret_cast<uintptr_t>(&encrypted_masked_syscall_stub), syscall_inst_addr, std::forward<ServiceArgs>(args)...);
    }
};

}// namespace pigsyscall

#endif //PIG_SYSCALL_HPP_
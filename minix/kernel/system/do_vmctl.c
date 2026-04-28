/* The kernel call implemented in this file:
 *   m_type:	SYS_VMCTL
 *
 * The parameters for this kernel call are:
 *   	SVMCTL_WHO	which process
 *    	SVMCTL_PARAM	set this setting (VMCTL_*)
 *    	SVMCTL_VALUE	to this value
 */

#include "kernel/system.h"
#include "kernel/vm.h"
#include <assert.h>
#include "kmutex.h"
#include "minix/abstract_pagetables.h"
#include "pagetables.h"
#include "mmap_utils.h"
#include "apt_utils.h"

extern struct kinfo kinfo;

extern int do_kyield(struct proc * caller, message * m_ptr);

/*===========================================================================*
 *				do_vmctl				     *
 *===========================================================================*/
int do_vmctl(struct proc * caller, message * m_ptr)
{
  int proc_nr;
  endpoint_t ep = m_ptr->SVMCTL_WHO;
  struct proc *p, *rp, **rpp, *target;

  if(ep == SELF) { ep = caller->p_endpoint; }

  if(!isokendpt(ep, &proc_nr)) {
	printf("do_vmctl: unexpected endpoint %d from VM\n", ep);
	return EINVAL;
  }

  p = proc_addr(proc_nr);

  switch(m_ptr->SVMCTL_PARAM) {
      // Получить виртуальный адрес таблицы APT для процесса
      case VMCTL_GET_APT:
          m_ptr->SVMCTL_APT = p->apt_table;
          return OK;

      // Обновить данные таблицы страниц согласно новой APT
      // и сбросить связанный кеш
          // но если мы не сможем сразу её закомитить, то мы так же как и с pagefault
          // Переложим эту ответственность на функцию переключения контекста
      case VMCTL_COMMIT_APT:
          if (kmutex_trylock(p->apt_table->lock)) {
              // сейчас обновим нашу физическую таблицу страниц
              vm_arch_apt_to_pt(kinfo.apt, p->apt_table, kinfo.arch_pagetables, p->pt_handler);
              proc_context_shoot_all(p->context_id); // Всегда убиваем кеш по контексту
              // Эта функция уже реализует SMP
              p->apt_version = p->apt_table->version;
              kmutex_unlock(p->apt_table->lock);
          } else {
              printf("do_vmctl: Process %d APT MUTEX Locked while VMCTL_COMMIT_APT\r\n", p->p_nr);
              // Мьютекс заблокирован, это странно, но мы обработаем это так
              // Мы просто переместим процесс в конец очереди, так как при изменении APT были изменена версия
              // Так что при следующем переключении контекста попытка синхронизировать адресное пространство повторится
              do_kyield(p, NULL);
          }
          return OK;

      // Установить для процесса новую таблицу APT
      // Предполагает что нужно её сразу закоммитить
      // но если мы не сможем сразу её закомитить, то мы так же как и с pagefault
      // Переложим эту ответственность на функцию переключения контекста
      case VMCTL_SET_APT:
          p->apt_table = m_ptr->SVMCTL_APT;
          if (kmutex_trylock(p->apt_table->lock)) {
              // сейчас обновим нашу физическую таблицу страниц
              vm_arch_apt_to_pt(kinfo.apt, p->apt_table, kinfo.arch_pagetables, p->pt_handler);
              proc_context_shoot_all(p->context_id); // Всегда убиваем кеш по контексту
              // Эта функция уже реализует SMP
              p->apt_version = p->apt_table->version;
              kmutex_unlock(p->apt_table->lock);
          } else {
              printf("do_vmctl: Process %d APT MUTEX Locked while VMCTL_SET_APT\r\n", p->p_nr);
              // Мьютекс заблокирован, это странно, но мы обработаем это так
              // Мы просто переместим процесс в конец очереди, так как при изменении APT были изменена версия
              // Так что при следующем переключении контекста попытка синхронизировать адресное пространство повторится
              do_kyield(p, NULL);
          }
          return OK;

    // Сбросить флаг ошибки адресации страниц для процесса
	case VMCTL_CLEAR_PAGEFAULT:
		assert(RTS_ISSET(p,RTS_PAGEFAULT));
        if (kmutex_trylock(p->apt_table->lock)) {
          // сейчас обновим нашу физическую таблицу страниц
            vm_arch_apt_to_pt(kinfo.apt, p->apt_table, kinfo.arch_pagetables, p->pt_handler);
            proc_context_shoot_all(p->context_id); // Всегда убиваем кеш по контексту
                                                    // Эта функция уже реализует SMP
            p->apt_version = p->apt_table->version;
            kmutex_unlock(p->apt_table->lock);
        } else {
          printf("do_vmctl: Process %d APT MUTEX Locked while VMCTL_CLEAR_PAGEFAULT\r\n", p->p_nr);
          // Мьютекс заблокирован, это странно, но мы обработаем это так
          // Мы просто переместим процесс в конец очереди, так как при изменении APT были изменена версия
          // Так что при следующем переключении контекста попытка синхронизировать адресное пространство повторится
          do_kyield(p, NULL);
        }
		RTS_UNSET(p, RTS_PAGEFAULT);
		return OK;

    // Получение списка запросов памяти от ядра
	case VMCTL_MEMREQ_GET:
		/* Send VM the information about the memory request. We can
		 * not simply send the first request on the list, because IPC
		 * filters may forbid VM from getting requests for particular
		 * sources. However, IPC filters are used only in rare cases.
		 */
		for (rpp = &vmrequest; *rpp != NULL;
		    rpp = &(*rpp)->p_vmrequest.nextrequestor) {
			rp = *rpp;

			assert(RTS_ISSET(rp, RTS_VMREQUEST));

			okendpt(rp->p_vmrequest.target, &proc_nr);
			target = proc_addr(proc_nr);

			/* Check against IPC filters. */
			if (!allow_ipc_filtered_memreq(rp, target))
				continue;

			/* Reply with request fields. */
			if (rp->p_vmrequest.req_type != VMPTYPE_CHECK)
				panic("VMREQUEST wrong type");

			m_ptr->SVMCTL_MRG_TARGET	=
				rp->p_vmrequest.target;
			m_ptr->SVMCTL_MRG_ADDR		=
				rp->p_vmrequest.params.check.start;
			m_ptr->SVMCTL_MRG_LENGTH	=
				rp->p_vmrequest.params.check.length;
			m_ptr->SVMCTL_MRG_FLAG		=
				rp->p_vmrequest.params.check.writeflag;
			m_ptr->SVMCTL_MRG_REQUESTOR	=
				(void *) rp->p_endpoint;

			rp->p_vmrequest.vmresult = VMSUSPEND;

			/* Remove from request chain. */
			*rpp = rp->p_vmrequest.nextrequestor;

			return rp->p_vmrequest.req_type;
		}

		return ENOENT;

    // Ответ о выделении памяти для ядерных нужд
	case VMCTL_MEMREQ_REPLY:
		assert(RTS_ISSET(p, RTS_VMREQUEST));
		assert(p->p_vmrequest.vmresult == VMSUSPEND);
  		okendpt(p->p_vmrequest.target, &proc_nr);
		target = proc_addr(proc_nr);
		p->p_vmrequest.vmresult = m_ptr->SVMCTL_VALUE;
		assert(p->p_vmrequest.vmresult != VMSUSPEND);

		switch(p->p_vmrequest.type) {
		case VMSTYPE_KERNELCALL:
			/*
			 * we will have to resume execution of the kernel call
			 * as soon the scheduler picks up this process again
			 */
			p->p_misc_flags |= MF_KCALL_RESUME;
			break;
		case VMSTYPE_DELIVERMSG:
			assert(p->p_misc_flags & MF_DELIVERMSG);
			assert(p == target);
			assert(RTS_ISSET(p, RTS_VMREQUEST));
			break;
		case VMSTYPE_MAP:
			assert(RTS_ISSET(p, RTS_VMREQUEST));
			break;
		default:
			panic("strange request type: %d",p->p_vmrequest.type);
		}

		RTS_UNSET(p, RTS_VMREQUEST);
		return OK;

    // Установка флага процесса, что он остановлен, до завершения действий VM
	case VMCTL_VMINHIBIT_SET:
		/* Проверочка что процесс на другом процессоре, если так, то мы отправим IPI */
		if (p->p_cpu != cpunr) {
            ipi_send_vm_inhibit(p);
		} else
			RTS_SET(p, RTS_VMINHIBIT);
		p->p_misc_flags |= MF_FLUSH_TLB;
		return OK;

    // Сброс флага состояния остановки по требованию VM
	case VMCTL_VMINHIBIT_CLEAR:
		assert(RTS_ISSET(p, RTS_VMINHIBIT));
		/*
		 * the processes is certainly not runnable, no need to tell its
		 * cpu
		 */
		RTS_UNSET(p, RTS_VMINHIBIT);
		if (p->p_misc_flags & MF_SENDA_VM_MISS) {
			struct priv *privp;
			p->p_misc_flags &= ~MF_SENDA_VM_MISS;
			privp = priv(p);
			try_deliver_senda(p, (asynmsg_t *) privp->s_asyntab,
							privp->s_asynsize);
		}
		/*
		 * We don't know whether kernel has the changed mapping
		 * installed to access userspace memory. And if so, on what CPU.
		 * More over we don't know what mapping has changed and how and
		 * therefore we must invalidate all mappings we have anywhere.
		 * Next time we map memory, we map it fresh.
		 */
		bits_fill(p->p_stale_tlb, CONFIG_MAX_CPUS);
		return OK;

	case VMCTL_CLEARMAPCACHE:
		/* VM says: forget about old mappings we have cached. */
		mem_clear_mapcache();
		return OK;

    // Сброс остановки процесса после старта системы
    // Первые процессы рождаются с этим флагом
    // И ждут когда VM для них разметит адресное пространство
	case VMCTL_BOOTINHIBIT_CLEAR:
		RTS_UNSET(p, RTS_BOOTINHIBIT);
		return OK;
  }

    printf("do_vmctl: strange param %d\n", m_ptr->SVMCTL_PARAM);
    return EINVAL;
}

# Thread safety

The core library does not create tasks and does not allocate memory. Thread safety comes from the way you call it.

## Client

`at_client_feed` is single-consumer. Call it from one RX task only. Do not call it from an ISR.

`at_client_exec` may run from other tasks when `at_sync_ops` is installed:

- `lock` / `unlock` serialize commands. One exchange is in flight per client. The lock does not need to be recursive, and it must not be taken from `at_client_feed`.
- `signal` runs on the RX task when the exchange finishes.
- `wait` runs on the task that called `at_client_exec`.

Publish order inside the client is: fill the response, store the status, clear the waiting flag, then signal. The task that returns from `wait` can read the response object. `volatile` plus a compiler barrier covers a single-core MCU. A multi-core port should make `signal` / `wait` a real semaphore (the FreeRTOS adapter does).

`at_client_abort` takes the same lock. Use it only for the non-blocking case (`at_client_exec` returned `AT_RESP_PENDING` because no `wait` callback is installed). Do not call it from the task that is blocked inside `wait`.

URC and idle-line callbacks run on the feed (RX) task. They must not call `at_client_exec` on that same client. The RX task would sit in `wait` and never feed the rest of the reply. Queue the work to another task instead.

Do not call `at_client_feed` re-entrantly. A nested call returns `AT_ERR_BUSY`.

## Server

`at_server_feed` is single-consumer, same rule as the client. Handlers run on that task. `argv` pointers die when the handler returns.

Reply helpers write the UART hook directly. The hook must be safe to call from the feed task. It should copy or finish the bytes before returning. Do not call `at_server_feed` again from a handler or from the write hook.

`at_server_register` is for startup, before the RX task runs.

## Ring and ISR

`at_ring_write` has one producer. `at_ring_read` has one consumer. The FreeRTOS adapter uses the UART ISR as the producer and the RX task as the consumer.

Indexes are `volatile` and are published with a compiler barrier after the byte copies. That is the usual single-core pattern. Multi-core hardware needs an acquire/release barrier (or a FreeRTOS stream buffer) in place of `AT_BARRIER()` in `src/at_internal.h`.

`at_rtos_rx_from_isr` is the only adapter call that is safe in an ISR. After it returns, pass `woken` to `portYIELD_FROM_ISR` when it is non-NULL.

The RX task notification belongs to the adapter. Do not call `ulTaskNotifyTake` or `vTaskNotifyGive` on that task for anything else.

## CLI mode

`at_client_enter_cli` / `at_client_exit_cli` should run when no command is waiting, from the task that owns the client. Raw bytes are delivered on the feed task.

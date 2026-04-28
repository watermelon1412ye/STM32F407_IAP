#include "ota_update.h"

#include <stdio.h>
#include <string.h>

#include "stm32f4xx.h"
#include "stm32f4xx_flash.h"
#include "iap_layout.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#define OTA_SERVER_PORT                 6000
#define OTA_SOCKET_RX_BUFFER_SIZE       1536
#define OTA_IDLE_FINISH_TIMEOUT_MS      1500U

#define OTA_HDR0                        0x5AU
#define OTA_HDR1                        0xA5U
#define OTA_CMD_FIRMWARE_H              0x00U
#define OTA_CMD_FIRMWARE_L              0x01U
#define OTA_CMD_ENTER_IAP_H             0x00U
#define OTA_CMD_ENTER_IAP_L             0x02U
#define OTA_FRAME_MAX_N                 256U
#define OTA_FRAME_OVERHEAD              12U

#define IAP_FLAG_MAGIC_VAL              0x424F4F54U
#define IAP_FLAG_VERSION_VAL            0x00000001U

typedef struct IapFlagBlock_s {
  uint32_t magic;
  uint32_t version;
  uint32_t boot_from;
  uint32_t image_size;
} IapFlagBlock_t;

typedef struct OtaSession_s {
  uint8_t active;
  uint8_t target_boot_from;
  uint32_t slot_start;
  uint32_t write_addr;
  uint32_t bytes_written;
  uint32_t last_rx_ms;
} OtaSession_t;

static uint32_t ota_now_ms(void)
{
  return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static uint16_t ota_crc16_modbus(const uint8_t *data, uint32_t len)
{
  uint16_t crc;
  uint32_t i;
  int bit;

  crc = 0xFFFFU;
  for (i = 0U; i < len; i++) {
    crc ^= data[i];
    for (bit = 0; bit < 8; bit++) {
      if ((crc & 1U) != 0U) {
        crc = (uint16_t)((crc >> 1) ^ 0xA001U);
      } else {
        crc = (uint16_t)(crc >> 1);
      }
    }
  }

  return crc;
}

static FLASH_Status ota_flash_program_word(uint32_t addr, uint32_t data)
{
  FLASH_Status st;

  (void)FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                        FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

  st = FLASH_ProgramWord(addr, data);
  if (st != FLASH_COMPLETE) {
    (void)FLASH_WaitForLastOperation();
    return st;
  }

  return FLASH_WaitForLastOperation();
}

static FLASH_Status ota_flash_program_byte(uint32_t addr, uint8_t data)
{
  FLASH_Status st;

  (void)FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                        FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

  st = FLASH_ProgramByte(addr, data);
  if (st != FLASH_COMPLETE) {
    (void)FLASH_WaitForLastOperation();
    return st;
  }

  return FLASH_WaitForLastOperation();
}

static FLASH_Status ota_flash_write_bytes(uint32_t start_addr, const uint8_t *data, uint16_t len)
{
  uint32_t addr;
  uint16_t idx;

  if ((data == NULL) || (len == 0U)) {
    return FLASH_COMPLETE;
  }

  addr = start_addr;
  idx = 0U;

  while (idx < len) {
    FLASH_Status st;

    if (((addr & 0x3U) == 0U) && ((uint16_t)(idx + 4U) <= len)) {
      uint32_t word_data;

      word_data = ((uint32_t)data[idx + 0U]) |
                  ((uint32_t)data[idx + 1U] << 8) |
                  ((uint32_t)data[idx + 2U] << 16) |
                  ((uint32_t)data[idx + 3U] << 24);

      st = ota_flash_program_word(addr, word_data);
      if (st != FLASH_COMPLETE) {
        return st;
      }

      addr += 4U;
      idx += 4U;
    } else {
      st = ota_flash_program_byte(addr, data[idx]);
      if (st != FLASH_COMPLETE) {
        return st;
      }

      addr += 1U;
      idx += 1U;
    }
  }

  return FLASH_COMPLETE;
}

static uint8_t ota_flag_valid(const IapFlagBlock_t *flag_block)
{
  return (flag_block->magic == IAP_FLAG_MAGIC_VAL) ? 1U : 0U;
}

static uint8_t ota_select_target_boot_from(void)
{
  const IapFlagBlock_t *flag_block;

  flag_block = (const IapFlagBlock_t *)IAP_FLAG_SECTOR_START;
  if (ota_flag_valid(flag_block) == 0U) {
    return 1U;
  }

  return (flag_block->boot_from == 0U) ? 1U : 0U;
}

static FLASH_Status ota_erase_slot(uint8_t boot_from)
{
  FLASH_Status st;

  if (boot_from == 0U) {
    st = FLASH_EraseSector(FLASH_Sector_6, VoltageRange_3);
    if (st != FLASH_COMPLETE) {
      return st;
    }

    st = FLASH_EraseSector(FLASH_Sector_7, VoltageRange_3);
    return st;
  }

  st = FLASH_EraseSector(FLASH_Sector_8, VoltageRange_3);
  if (st != FLASH_COMPLETE) {
    return st;
  }

  st = FLASH_EraseSector(FLASH_Sector_9, VoltageRange_3);
  return st;
}

static FLASH_Status ota_write_flag(uint8_t boot_from, uint32_t image_size)
{
  IapFlagBlock_t flag_block;
  FLASH_Status st;
  const uint32_t *words;
  uint32_t addr;
  uint32_t i;

  flag_block.magic = IAP_FLAG_MAGIC_VAL;
  flag_block.version = IAP_FLAG_VERSION_VAL;
  flag_block.boot_from = boot_from;
  flag_block.image_size = image_size;

  st = FLASH_EraseSector(FLASH_Sector_3, VoltageRange_3);
  if (st != FLASH_COMPLETE) {
    return st;
  }

  words = (const uint32_t *)&flag_block;
  addr = IAP_FLAG_SECTOR_START;
  for (i = 0U; i < (sizeof(IapFlagBlock_t) / sizeof(uint32_t)); i++) {
    st = ota_flash_program_word(addr, words[i]);
    if (st != FLASH_COMPLETE) {
      return st;
    }
    addr += 4U;
  }

  return FLASH_COMPLETE;
}

static uint32_t ota_slot_start_from_boot_from(uint8_t boot_from)
{
  return (boot_from == 0U) ? IAP_SLOT_A_START : IAP_SLOT_B_START;
}

static void ota_session_reset(OtaSession_t *session)
{
  memset(session, 0, sizeof(*session));
}

static int ota_begin_session(OtaSession_t *session)
{
  FLASH_Status st;

  ota_session_reset(session);
  session->target_boot_from = ota_select_target_boot_from();
  session->slot_start = ota_slot_start_from_boot_from(session->target_boot_from);
  session->write_addr = session->slot_start;
  session->last_rx_ms = ota_now_ms();

  printf("OTA: prepare slot %c at 0x%08lX\r\n",
         (session->target_boot_from == 0U) ? 'A' : 'B',
         (unsigned long)session->slot_start);

  FLASH_Unlock();
  st = ota_erase_slot(session->target_boot_from);
  FLASH_Lock();

  if (st != FLASH_COMPLETE) {
    printf("OTA error: erase slot failed, st=%d\r\n", (int)st);
    ota_session_reset(session);
    return -1;
  }

  session->active = 1U;
  return 0;
}

static int ota_write_chunk(OtaSession_t *session, const uint8_t *data, uint16_t len)
{
  FLASH_Status st;

  if (session->active == 0U) {
    if (ota_begin_session(session) != 0) {
      return -1;
    }
  }

  if ((session->bytes_written + (uint32_t)len) > IAP_RUN_SIZE) {
    printf("OTA error: image too large (%lu bytes)\r\n",
           (unsigned long)(session->bytes_written + (uint32_t)len));
    return -1;
  }

  FLASH_Unlock();
  st = ota_flash_write_bytes(session->write_addr, data, len);
  FLASH_Lock();

  if (st != FLASH_COMPLETE) {
    printf("OTA error: flash write failed at 0x%08lX, st=%d\r\n",
           (unsigned long)session->write_addr,
           (int)st);
    return -1;
  }

  session->write_addr += len;
  session->bytes_written += len;
  session->last_rx_ms = ota_now_ms();

  printf("OTA: slot %c progress %lu bytes\r\n",
         (session->target_boot_from == 0U) ? 'A' : 'B',
         (unsigned long)session->bytes_written);

  return 0;
}

static void ota_commit_and_reboot(OtaSession_t *session)
{
  FLASH_Status st;

  if ((session->active == 0U) || (session->bytes_written == 0U)) {
    return;
  }

  printf("OTA: finalize slot %c, image_size=%lu\r\n",
         (session->target_boot_from == 0U) ? 'A' : 'B',
         (unsigned long)session->bytes_written);

  FLASH_Unlock();
  st = ota_write_flag(session->target_boot_from, session->bytes_written);
  FLASH_Lock();

  if (st != FLASH_COMPLETE) {
    printf("OTA error: write flag failed, st=%d\r\n", (int)st);
    ota_session_reset(session);
    return;
  }

  printf("OTA: flag updated, reboot to bootloader copy flow.\r\n");
  vTaskDelay(pdMS_TO_TICKS(100));
  NVIC_SystemReset();
}

static int ota_process_frame(OtaSession_t *session,
                             uint8_t cmd_h,
                             uint8_t cmd_l,
                             const uint8_t *payload,
                             uint16_t payload_len)
{
  if ((cmd_h == OTA_CMD_ENTER_IAP_H) && (cmd_l == OTA_CMD_ENTER_IAP_L)) {
    printf("OTA: ENTER_IAP received over TCP.\r\n");
    return ota_begin_session(session);
  }

  if ((cmd_h == OTA_CMD_FIRMWARE_H) && (cmd_l == OTA_CMD_FIRMWARE_L)) {
    return ota_write_chunk(session, payload, payload_len);
  }

  printf("OTA error: unknown cmd 0x%02X 0x%02X\r\n",
         (unsigned)cmd_h,
         (unsigned)cmd_l);
  return -1;
}

static void ota_consume_bytes(uint8_t *buffer, uint16_t *buffer_len, uint16_t consume_len)
{
  if (consume_len >= *buffer_len) {
    *buffer_len = 0U;
    return;
  }

  memmove(buffer, buffer + consume_len, (size_t)(*buffer_len - consume_len));
  *buffer_len = (uint16_t)(*buffer_len - consume_len);
}

static void ota_parse_stream(OtaSession_t *session, uint8_t *buffer, uint16_t *buffer_len)
{
  while (*buffer_len >= 2U) {
    uint16_t payload_len;
    uint16_t frame_len;
    uint16_t crc_offset;
    uint16_t crc_rx;
    uint16_t crc_calc;

    if ((buffer[0] != OTA_HDR0) || (buffer[1] != OTA_HDR1)) {
      ota_consume_bytes(buffer, buffer_len, 1U);
      continue;
    }

    if (*buffer_len < 6U) {
      return;
    }

    payload_len = (uint16_t)(((uint16_t)buffer[4] << 8) | buffer[5]);
    if (payload_len > OTA_FRAME_MAX_N) {
      printf("OTA error: invalid payload length %u\r\n", (unsigned)payload_len);
      ota_consume_bytes(buffer, buffer_len, 2U);
      continue;
    }

    frame_len = (uint16_t)(payload_len + OTA_FRAME_OVERHEAD);
    if (*buffer_len < frame_len) {
      return;
    }

    crc_offset = (uint16_t)(10U + payload_len);
    crc_calc = ota_crc16_modbus(buffer, crc_offset);
    crc_rx = (uint16_t)buffer[crc_offset] |
             (uint16_t)((uint16_t)buffer[crc_offset + 1U] << 8);

    if (crc_calc != crc_rx) {
      printf("OTA error: CRC mismatch calc=0x%04X rx=0x%04X\r\n", crc_calc, crc_rx);
      ota_consume_bytes(buffer, buffer_len, 2U);
      continue;
    }

    if (ota_process_frame(session,
                          buffer[2],
                          buffer[3],
                          &buffer[6],
                          payload_len) != 0) {
      ota_session_reset(session);
    }

    ota_consume_bytes(buffer, buffer_len, frame_len);
  }
}

void OTA_UpdateTask(void *parameter)
{
  int listen_fd;
  struct sockaddr_in local_addr;
  OtaSession_t session;

  (void)parameter;
  ota_session_reset(&session);

  for (;;) {
    listen_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
      printf("OTA error: socket create failed\r\n");
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(OTA_SERVER_PORT);
    local_addr.sin_addr.s_addr = PP_HTONL(INADDR_ANY);

    if (lwip_bind(listen_fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
      printf("OTA error: bind failed\r\n");
      lwip_close(listen_fd);
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (lwip_listen(listen_fd, 1) != 0) {
      printf("OTA error: listen failed\r\n");
      lwip_close(listen_fd);
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    printf("OTA: TCP server listening on port %d\r\n", OTA_SERVER_PORT);

    for (;;) {
      int conn_fd;
      struct sockaddr_in client_addr;
      int client_len;
      struct timeval recv_timeout;
      uint8_t rx_buffer[OTA_SOCKET_RX_BUFFER_SIZE];
      uint16_t rx_len;

      client_len = (int)sizeof(client_addr);
      conn_fd = lwip_accept(listen_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_len);
      if (conn_fd < 0) {
        break;
      }

      printf("OTA: client connected %s:%u\r\n",
             inet_ntoa(client_addr.sin_addr),
             (unsigned)ntohs(client_addr.sin_port));

      recv_timeout.tv_sec = 0;
      recv_timeout.tv_usec = 200000;
      (void)lwip_setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

      rx_len = 0U;
      ota_session_reset(&session);

      for (;;) {
        int recv_len;

        recv_len = lwip_recv(conn_fd,
                             (void *)(rx_buffer + rx_len),
                             (int)(sizeof(rx_buffer) - rx_len),
                             0);

        if (recv_len > 0) {
          rx_len = (uint16_t)(rx_len + (uint16_t)recv_len);
          session.last_rx_ms = ota_now_ms();
          ota_parse_stream(&session, rx_buffer, &rx_len);
          continue;
        }

        if (recv_len == 0) {
          printf("OTA: client disconnected\r\n");
          break;
        }

        if ((session.active != 0U) &&
            (session.bytes_written > 0U) &&
            ((ota_now_ms() - session.last_rx_ms) >= OTA_IDLE_FINISH_TIMEOUT_MS)) {
          lwip_close(conn_fd);
          ota_commit_and_reboot(&session);
          break;
        }

        /* lwIP 1.4 timeout path returns <0 here; for the minimal OTA loop we
           only care about "idle long enough to commit", so keep waiting. */
        vTaskDelay(pdMS_TO_TICKS(20));
      }

      lwip_close(conn_fd);

      if ((session.active != 0U) &&
          (session.bytes_written > 0U) &&
          ((ota_now_ms() - session.last_rx_ms) >= OTA_IDLE_FINISH_TIMEOUT_MS)) {
        ota_commit_and_reboot(&session);
      }
    }

    lwip_close(listen_fd);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

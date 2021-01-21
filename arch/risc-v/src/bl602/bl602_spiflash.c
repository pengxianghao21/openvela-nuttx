/****************************************************************************
 * arch/risc-v/src/bl602/bl602_spiflash.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <syslog.h>

#ifdef CONFIG_BL602_SPIFLASH
#include <nuttx/arch.h>
#include <nuttx/init.h>
#include <nuttx/irq.h>
#include <nuttx/semaphore.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/timers/timer.h>

#include <arch/board/board.h>

#include <bl602_flash.h>
#include <bl602_spiflash.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SPIFLASH_BLOCKSIZE          (0x1000)
#define MTD2PRIV(_dev)              ((FAR struct bl602_spiflash_s *)_dev)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* SPI Flash device hardware configuration */

struct bl602_spiflash_config_s
{
  /* SPI register base address */

  uint32_t flash_offset;
  uint32_t flash_size;
  uint32_t flash_offset_xip;
};

/* SPI Flash device private data  */

struct bl602_spiflash_s
{
  struct mtd_dev_s mtd;
  struct bl602_spiflash_config_s *config;
};

/****************************************************************************
 * ROM function prototypes
 ****************************************************************************/

/* MTD driver methods */

static int bl602_erase(FAR struct mtd_dev_s *dev, off_t startblock,
                       size_t nblocks);
static ssize_t bl602_bread(FAR struct mtd_dev_s *dev, off_t startblock,
                           size_t nblocks, FAR uint8_t *buffer);
static ssize_t bl602_bwrite(FAR struct mtd_dev_s *dev, off_t startblock,
                            size_t nblocks, FAR const uint8_t *buffer);
static ssize_t bl602_read(FAR struct mtd_dev_s *dev, off_t offset,
                          size_t nbytes, FAR uint8_t *buffer);
static int bl602_ioctl(FAR struct mtd_dev_s *dev, int cmd,
                       unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bl602_spiflash_config_s g_bl602_spiflash_config =
{
    .flash_offset = 0,
    .flash_size = 0,
    .flash_offset_xip = 0,
};

static struct bl602_spiflash_s g_bl602_spiflash =
{
  .mtd =
    {
      .erase  = bl602_erase,
      .bread  = bl602_bread,
      .bwrite = bl602_bwrite,
      .read   = bl602_read,
      .ioctl  = bl602_ioctl,
      .name   = "bl602_media"
    },
  .config = &g_bl602_spiflash_config,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl602_erase
 *
 * Description:
 *   Erase SPI Flash designated sectors.
 *
 * Input Parameters:
 *   dev        - bl602 MTD device data
 *   startblock - start block number, it is not equal to SPI Flash's block
 *   nblocks    - blocks number
 *
 * Returned Value:
 *   0 if success or a negative value if fail.
 *
 ****************************************************************************/

static int bl602_erase(FAR struct mtd_dev_s *dev, off_t startblock,
                       size_t nblocks)
{
  int ret = 0;
  FAR struct bl602_spiflash_s *priv = MTD2PRIV(dev);
  uint32_t addr = priv->config->flash_offset \
                  + startblock * SPIFLASH_BLOCKSIZE;
  uint32_t size = nblocks * SPIFLASH_BLOCKSIZE;

  syslog(LOG_INFO, "bl602_erase dev=%p, addr=0x%lx, size=0x%lx\r\n",
      dev, addr, size);

  ret = bl602_flash_erase(addr, size);

  if (ret == 0)
    {
      ret = nblocks;
    }

  return ret;
}

/****************************************************************************
 * Name: bl602_read
 *
 * Description:
 *   Read data from SPI Flash at designated address.
 *
 * Input Parameters:
 *   dev    - bl602 MTD device data
 *   offset - target address offset
 *   nbytes - data number
 *   buffer - data buffer pointer
 *
 * Returned Value:
 *   Read data bytes if success or a negative value if fail.
 *
 ****************************************************************************/

static ssize_t bl602_read(FAR struct mtd_dev_s *dev, off_t offset,
                          size_t nbytes, FAR uint8_t *buffer)
{
  int ret = 0;
  FAR struct bl602_spiflash_s *priv = MTD2PRIV(dev);
  uint32_t addr = priv->config->flash_offset + offset;
  uint32_t size = nbytes;

  syslog(LOG_INFO, "bl602_read dev=%p, addr=0x%lx, size=0x%lx\r\n",
      dev, addr, size);

  if (0 == bl602_flash_read(addr, buffer, size))
    {
      return ret = size;
    }

  return (ssize_t)ret;
}

/****************************************************************************
 * Name: bl602_bread
 *
 * Description:
 *   Read data from designated blocks.
 *
 * Input Parameters:
 *   dev        - bl602 MTD device data
 *   startblock - start block number, it is not equal to SPI Flash's block
 *   nblocks    - blocks number
 *   buffer     - data buffer pointer
 *
 * Returned Value:
 *   Read block number if success or a negative value if fail.
 *
 ****************************************************************************/

static ssize_t bl602_bread(FAR struct mtd_dev_s *dev, off_t startblock,
                           size_t nblocks, FAR uint8_t *buffer)
{
  int ret = 0;
  FAR struct bl602_spiflash_s *priv = MTD2PRIV(dev);
  uint32_t addr = priv->config->flash_offset \
                  + startblock * SPIFLASH_BLOCKSIZE;
  uint32_t size = nblocks * SPIFLASH_BLOCKSIZE;

  syslog(LOG_INFO, "bl602_bread dev=%p, addr=0x%lx, size=0x%lx\r\n",
      dev, addr, size);

  if (0 == bl602_flash_read(addr, buffer, size))
    {
      ret = nblocks;
    }

  return (ssize_t)ret;
}

/****************************************************************************
 * Name: bl602_bwrite
 *
 * Description:
 *   Write data to designated blocks.
 *
 * Input Parameters:
 *   dev        - bl602 MTD device data
 *   startblock - start MTD block number,
 *                it is not equal to SPI Flash's block
 *   nblocks    - blocks number
 *   buffer     - data buffer pointer
 *
 * Returned Value:
 *   Writen block number if success or a negative value if fail.
 *
 ****************************************************************************/

static ssize_t bl602_bwrite(FAR struct mtd_dev_s *dev, off_t startblock,
                            size_t nblocks, FAR const uint8_t *buffer)
{
  int ret = 0;
  FAR struct bl602_spiflash_s *priv = MTD2PRIV(dev);
  uint32_t addr = priv->config->flash_offset \
                  + startblock * SPIFLASH_BLOCKSIZE;
  uint32_t size = nblocks * SPIFLASH_BLOCKSIZE;

  syslog(LOG_INFO, "bl602_bwrite dev=%p, addr=0x%lx, size=0x%lx\r\n",
      dev, addr, size);

  if (0 == bl602_flash_write(addr, buffer, size))
    {
      ret = nblocks;
    }

  return (ssize_t)ret;
}

/****************************************************************************
 * Name: bl602_ioctl
 *
 * Description:
 *   Set/Get option to/from bl602 SPI Flash MTD device data.
 *
 * Input Parameters:
 *   dev - bl602 MTD device data
 *   cmd - operation command
 *   arg - operation argument
 *
 * Returned Value:
 *   0 if success or a negative value if fail.
 *
 ****************************************************************************/

int bl602_ioctl(FAR struct mtd_dev_s *dev, int cmd,
                       unsigned long arg)
{
  int ret = -EINVAL;
  FAR struct bl602_spiflash_s *priv = MTD2PRIV(dev);
  FAR struct mtd_geometry_s *geo;

  switch (cmd)
    {
      case MTDIOC_GEOMETRY:
        {
          syslog(LOG_INFO, "cmd(0x%x) MTDIOC_GEOMETRY.\r\n", cmd);
          geo = (FAR struct mtd_geometry_s *)arg;
          if (geo)
            {
              geo->blocksize    = SPIFLASH_BLOCKSIZE;
              geo->erasesize    = SPIFLASH_BLOCKSIZE;
              geo->neraseblocks = (priv->config->flash_size) / \
                                  SPIFLASH_BLOCKSIZE;
              ret               = OK;

              syslog(LOG_INFO,
                    "blocksize: %ld erasesize: %ld neraseblocks: %ld\n",
                    geo->blocksize, geo->erasesize, geo->neraseblocks);
            }
        }
        break;
      case MTDIOC_BULKERASE:
        {
          /* Erase the entire partition */

          syslog(LOG_INFO,
            "cmd(0x%x) MTDIOC_BULKERASE not support.\r\n", cmd);
        }
        break;
      case MTDIOC_XIPBASE:
        {
          /* Get the XIP base of the entire FLASH */

          syslog(LOG_INFO,
            "cmd(0x%x) MTDIOC_XIPBASE not support.\r\n", cmd);
        }
        break;
      case BIOC_FLUSH:
        {
          syslog(LOG_INFO, "cmd(0x%x) BIOC_FLUSH.\r\n", cmd);
          ret = OK;
        }
        break;
      default:
        {
          syslog(LOG_INFO, "cmd(0x%x) not support.\r\n", cmd);
          ret = -ENOTTY;
        }
        break;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl602_spiflash_alloc_mtdpart
 *
 * Description:
 *   Alloc bl602 SPI Flash MTD
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   bl602 SPI Flash MTD data pointer if success or NULL if fail
 *
 ****************************************************************************/

FAR struct mtd_dev_s *bl602_spiflash_alloc_mtdpart(void)
{
  struct bl602_spiflash_s *priv = &g_bl602_spiflash;
  FAR struct mtd_dev_s *mtd_part = NULL;

  priv->config->flash_offset = CONFIG_BL602_MTD_OFFSET;
  priv->config->flash_size = CONFIG_BL602_MTD_SIZE;

  mtd_part = mtd_partition(&priv->mtd, 0,
    CONFIG_BL602_MTD_SIZE / SPIFLASH_BLOCKSIZE);
  if (!mtd_part)
    {
      syslog(LOG_ERR, "ERROR: create MTD partition");
      return NULL;
    }

  return mtd_part;
}

/****************************************************************************
 * Name: bl602_spiflash_get_mtd
 *
 * Description:
 *   Get bl602 SPI Flash raw MTD.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   bl602 SPI Flash raw MTD data pointer.
 *
 ****************************************************************************/

FAR struct mtd_dev_s *bl602_spiflash_get_mtd(void)
{
  struct bl602_spiflash_s *priv = &g_bl602_spiflash;

  return &priv->mtd;
}

#endif /* CONFIG_BL602_SPIFLASH */

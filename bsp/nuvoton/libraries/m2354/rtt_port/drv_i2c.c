/**************************************************************************//**
*
* @copyright (C) 2020 Nuvoton Technology Corp. All rights reserved.
*
* SPDX-License-Identifier: Apache-2.0
*
* Change Logs:
* Date            Author         Notes
* 2020-2-05       HPHuang        First version
******************************************************************************/


#include <rtconfig.h>

#ifdef BSP_USING_I2C
#include <rtdevice.h>
#include <NuMicro.h>

/* Private define ---------------------------------------------------------------*/
#define LOG_TAG         "drv.i2c"
#define DBG_ENABLE
#define DBG_SECTION_NAME "drv.i2c"
#define DBG_LEVEL DBG_ERROR
#define DBG_COLOR
#include <rtdbg.h>

const rt_uint32_t u32I2C_MASTER_STATUS_START                  = 0x08UL;
const rt_uint32_t u32I2C_MASTER_STATUS_REPEAT_START           = 0x10UL;
const rt_uint32_t u32I2C_MASTER_STATUS_TRANSMIT_ADDRESS_ACK   = 0x18UL;
const rt_uint32_t u32I2C_MASTER_STATUS_TRANSMIT_ADDRESS_NACK  = 0x20UL;
const rt_uint32_t u32I2C_MASTER_STATUS_TRANSMIT_DATA_ACK      = 0x28UL;
const rt_uint32_t u32I2C_MASTER_STATUS_TRANSMIT_DATA_NACK     = 0x30UL;
const rt_uint32_t u32I2C_MASTER_STATUS_ARBITRATION_LOST       = 0x38UL;
const rt_uint32_t u32I2C_MASTER_STATUS_RECEIVE_ADDRESS_ACK    = 0x40UL;
const rt_uint32_t u32I2C_MASTER_STATUS_RECEIVE_ADDRESS_NACK   = 0x48UL;
const rt_uint32_t u32I2C_MASTER_STATUS_RECEIVE_DATA_ACK       = 0x50UL;
const rt_uint32_t u32I2C_MASTER_STATUS_RECEIVE_DATA_NACK      = 0x58UL;
const rt_uint32_t u32I2C_MASTER_STATUS_BUS_ERROR              = 0x00UL;
const rt_uint32_t u32I2C_MASTER_STATUS_BUS_RELEASED           = 0xF8UL;

/* Private typedef --------------------------------------------------------------*/
typedef struct _nu_i2c_bus
{
    struct rt_i2c_bus_device parent;
    I2C_T *I2C;
    struct rt_i2c_msg *msg;
    char *device_name;
} nu_i2c_bus_t;

/* Private variables ------------------------------------------------------------*/
#ifdef BSP_USING_I2C0
#define I2C0BUS_NAME  "i2c0"
static nu_i2c_bus_t nu_i2c0 =
{
    .I2C = I2C0,
    .device_name = I2C0BUS_NAME,
};
#endif /* BSP_USING_I2C0 */

#ifdef BSP_USING_I2C1
#define I2C1BUS_NAME  "i2c1"
static nu_i2c_bus_t nu_i2c1 =
{
    .I2C = I2C1,
    .device_name = I2C1BUS_NAME,
};
#endif /* BSP_USING_I2C1 */

#ifdef BSP_USING_I2C2
#define I2C2BUS_NAME  "i2c2"
static nu_i2c_bus_t nu_i2c2 =
{
    .I2C = I2C2,
    .device_name = I2C2BUS_NAME,
};
#endif /* BSP_USING_I2C2 */

/* Private functions ------------------------------------------------------------*/
#if (defined(BSP_USING_I2C0) || defined(BSP_USING_I2C1) || defined(BSP_USING_I2C2))

static rt_size_t nu_i2c_mst_xfer(struct rt_i2c_bus_device *bus,
                                 struct rt_i2c_msg msgs[],
                                 rt_uint32_t num);

static const struct rt_i2c_bus_device_ops nu_i2c_ops =
{
    .master_xfer        = nu_i2c_mst_xfer,
    .slave_xfer         = NULL,
    .i2c_bus_control    = NULL,
};

static rt_err_t nu_i2c_configure(nu_i2c_bus_t *bus)
{
    RT_ASSERT(bus != RT_NULL);

    bus->parent.ops = &nu_i2c_ops;
    I2C_Open(bus->I2C, 100000);

    return RT_EOK;
}

static inline rt_err_t nu_i2c_wait_ready_with_timeout(nu_i2c_bus_t *bus)
{
    rt_tick_t start = rt_tick_get();
    while (!(bus->I2C->CTL0 & I2C_CTL0_SI_Msk))
    {
        if ((rt_tick_get() - start) > bus->parent.timeout)
        {
            LOG_E("\ni2c: timeout!\n");
            return -RT_ETIMEOUT;
        }
    }

    return RT_EOK;
}

static inline rt_err_t nu_i2c_send_data(nu_i2c_bus_t *nu_i2c, rt_uint8_t data)
{
    I2C_SET_DATA(nu_i2c->I2C, data);
    I2C_SET_CONTROL_REG(nu_i2c->I2C, I2C_CTL_SI);
    return nu_i2c_wait_ready_with_timeout(nu_i2c);
}

static rt_err_t nu_i2c_send_address(nu_i2c_bus_t *nu_i2c,
                                    struct rt_i2c_msg  *msg)
{
    rt_uint16_t flags = msg->flags;
    rt_uint16_t ignore_nack = msg->flags & RT_I2C_IGNORE_NACK;
    rt_uint8_t addr1, addr2;
    rt_err_t ret;


    if (flags & RT_I2C_ADDR_10BIT)
    {
        nu_i2c->I2C->CTL1 |= I2C_CTL1_ADDR10EN_Msk;
        addr1 = 0xf0 | ((msg->addr >> 7) & 0x06);
        addr2 = msg->addr & 0xff;

        LOG_D("address1: %d, address2: %d\n", addr1, addr2);

        ret = nu_i2c_send_data(nu_i2c, addr1);
        if (ret != RT_EOK) /* for timeout condition */
            return -RT_EIO;

        if ((I2C_GET_STATUS(nu_i2c->I2C) != u32I2C_MASTER_STATUS_TRANSMIT_ADDRESS_ACK) && !ignore_nack)
        {
            LOG_E("NACK: sending first address failed\n");

            return -RT_EIO;
        }

        ret = nu_i2c_send_data(nu_i2c,  addr2);
        if (ret != RT_EOK) /* for timeout condition */
            return -RT_EIO;

        if ((I2C_GET_STATUS(nu_i2c->I2C) != u32I2C_MASTER_STATUS_TRANSMIT_ADDRESS_ACK) && !ignore_nack)
        {
            LOG_E("NACK: sending second address failed\n");

            return -RT_EIO;
        }

        if (flags & RT_I2C_RD)
        {
            LOG_D("send repeated START signal\n");

            I2C_SET_CONTROL_REG(nu_i2c->I2C, I2C_CTL_STA_SI);
            ret = nu_i2c_wait_ready_with_timeout(nu_i2c);
            if (ret != RT_EOK) /* for timeout condition */
                return -RT_EIO;

            if ((I2C_GET_STATUS(nu_i2c->I2C) != u32I2C_MASTER_STATUS_REPEAT_START) && !ignore_nack)
            {
                LOG_E("sending repeated START failed\n");

                return -RT_EIO;
            }

            addr1 |= 0x01;

            ret = nu_i2c_send_data(nu_i2c,  addr1);
            if (ret != RT_EOK) /* for timeout condition */
                return -RT_EIO;

            if ((I2C_GET_STATUS(nu_i2c->I2C) != u32I2C_MASTER_STATUS_RECEIVE_ADDRESS_ACK) && !ignore_nack)
            {
                LOG_E("NACK: sending read address failed\n");

                return -RT_EIO;
            }
        }
    }
    else
    {
        /* 7-bit addr */
        addr1 = msg->addr << 1;
        if (flags & RT_I2C_RD)
            addr1 |= 1;

        /* Send device address */
        ret = nu_i2c_send_data(nu_i2c,  addr1); /* Send Address */
        if (ret != RT_EOK) /* for timeout condition */
            return -RT_EIO;

        if ((I2C_GET_STATUS(nu_i2c->I2C)
                != ((flags & RT_I2C_RD) ? u32I2C_MASTER_STATUS_RECEIVE_ADDRESS_ACK : u32I2C_MASTER_STATUS_TRANSMIT_ADDRESS_ACK))
                && !ignore_nack)
        {
            LOG_E("sending address failed\n");
            return -RT_EIO;
        }
    }

    return RT_EOK;
}

static rt_size_t nu_i2c_mst_xfer(struct rt_i2c_bus_device *bus,
                                 struct rt_i2c_msg msgs[],
                                 rt_uint32_t num)
{
    struct rt_i2c_msg *msg;
    nu_i2c_bus_t *nu_i2c;
    rt_size_t i;
    rt_uint32_t cnt_data;
    rt_uint16_t ignore_nack;
    rt_err_t ret;

    RT_ASSERT(bus != RT_NULL);
    nu_i2c = (nu_i2c_bus_t *) bus;

    nu_i2c->msg = msgs;

    nu_i2c->I2C->CTL0 |= I2C_CTL0_STA_Msk | I2C_CTL0_SI_Msk;
    ret = nu_i2c_wait_ready_with_timeout(nu_i2c);
    if (ret != RT_EOK) /* for timeout condition */
    {
        rt_set_errno(-RT_ETIMEOUT);
        return 0;
    }
    if (I2C_GET_STATUS(nu_i2c->I2C) != u32I2C_MASTER_STATUS_START)
    {
        i = 0;
        LOG_E("Send START Failed");
        return i;
    }

    for (i = 0; i < num; i++)
    {
        msg = &msgs[i];
        ignore_nack = msg->flags & RT_I2C_IGNORE_NACK;

        if (!(msg->flags & RT_I2C_NO_START))
        {
            if (i)
            {
                I2C_SET_CONTROL_REG(nu_i2c->I2C, I2C_CTL_STA_SI);
                ret = nu_i2c_wait_ready_with_timeout(nu_i2c);
                if (ret != RT_EOK) /* for timeout condition */
                    break;

                if (I2C_GET_STATUS(nu_i2c->I2C) != u32I2C_MASTER_STATUS_REPEAT_START)
                {
                    i = 0;
                    LOG_E("Send repeat START Fail");
                    break;
                }
            }

            if ((RT_EOK != nu_i2c_send_address(nu_i2c, msg))
                    && !ignore_nack)
            {
                i = 0;
                LOG_E("Send Address Fail");
                break;
            }
        }


        if (nu_i2c->msg[i].flags & RT_I2C_RD) /* Receive Bytes */
        {
            rt_uint32_t do_rd_nack = (i == (num - 1));
            for (cnt_data = 0 ; cnt_data < (nu_i2c->msg[i].len) ; cnt_data++)
            {
                do_rd_nack += (cnt_data == (nu_i2c->msg[i].len - 1)); /* NACK after last byte  for hardware setting */
                if (do_rd_nack == 2)
                {
                    I2C_SET_CONTROL_REG(nu_i2c->I2C, I2C_CTL_SI);
                }
                else
                {
                    I2C_SET_CONTROL_REG(nu_i2c->I2C, I2C_CTL_SI_AA);
                }

                ret = nu_i2c_wait_ready_with_timeout(nu_i2c);
                if (ret != RT_EOK) /* for timeout condition */
                    break;

                if (nu_i2c->I2C->CTL0 & I2C_CTL_AA)
                {
                    if (I2C_GET_STATUS(nu_i2c->I2C) != u32I2C_MASTER_STATUS_RECEIVE_DATA_ACK)
                    {
                        i = 0;
                        break;
                    }
                }
                else
                {
                    if (I2C_GET_STATUS(nu_i2c->I2C) != u32I2C_MASTER_STATUS_RECEIVE_DATA_NACK)
                    {
                        i = 0;
                        break;
                    }
                }

                nu_i2c->msg[i].buf[cnt_data] = nu_i2c->I2C->DAT;
            }
        }
        else /* Send Bytes */
        {
            for (cnt_data = 0 ; cnt_data < (nu_i2c->msg[i].len) ; cnt_data++)
            {
                /* Send register number and MSB of data */
                ret = nu_i2c_send_data(nu_i2c, (uint8_t)(nu_i2c->msg[i].buf[cnt_data]));
                if (ret != RT_EOK) /* for timeout condition */
                    break;

                if (I2C_GET_STATUS(nu_i2c->I2C) != u32I2C_MASTER_STATUS_TRANSMIT_DATA_ACK
                        && !ignore_nack
                   ) /* Send aata and get Ack */
                {
                    i = 0;
                    break;
                }
            }
        }
    }

    I2C_STOP(nu_i2c->I2C);

    RT_ASSERT(I2C_GET_STATUS(nu_i2c->I2C) == u32I2C_MASTER_STATUS_BUS_RELEASED);
    if (I2C_GET_STATUS(nu_i2c->I2C) != u32I2C_MASTER_STATUS_BUS_RELEASED)
    {
        i = 0;
    }

    nu_i2c->msg = RT_NULL;
    nu_i2c->I2C->CTL1 = 0; /*clear all sub modes like 10 bit mode*/
    return i;
}

#endif

/* Public functions -------------------------------------------------------------*/
int rt_hw_i2c_init(void)
{
    rt_err_t ret = RT_ERROR;
#if   defined(BSP_USING_I2C0)
    SYS_UnlockReg();
    SYS_ResetModule(I2C0_RST);
    SYS_LockReg();
    nu_i2c_configure(&nu_i2c0);
    ret = rt_i2c_bus_device_register(&nu_i2c0.parent, nu_i2c0.device_name);
    RT_ASSERT(RT_EOK == ret);
#endif  /* BSP_USING_I2C0 */

#if   defined(BSP_USING_I2C1)
    SYS_UnlockReg();
    SYS_ResetModule(I2C1_RST);
    SYS_LockReg();
    nu_i2c_configure(&nu_i2c1);
    ret = rt_i2c_bus_device_register(&nu_i2c1.parent, nu_i2c1.device_name);
    RT_ASSERT(RT_EOK == ret);
#endif  /* BSP_USING_I2C1 */

#if   defined(BSP_USING_I2C2)
    SYS_UnlockReg();
    SYS_ResetModule(I2C2_RST);
    SYS_LockReg();
    nu_i2c_configure(&nu_i2c2);
    ret = rt_i2c_bus_device_register(&nu_i2c2.parent, nu_i2c2.device_name);
    RT_ASSERT(RT_EOK == ret);
#endif  /* BSP_USING_I2C2 */

    return ret;
}

INIT_DEVICE_EXPORT(rt_hw_i2c_init);

#endif /* BSP_USING_I2C */


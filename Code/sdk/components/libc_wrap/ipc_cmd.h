#ifndef __IPC_CMD_H__
#define __IPC_CMD_H__

#define CMD_RET_SUCCESS 1
#define CMD_RET_FAIL    0

/*******************************************************************************
** MACROS
*******************************************************************************/

#define KEY_RELEASE 0
#define KEY_PRESS   1
// #define KEY_HOLD    2

#define ENCRYPT                     1
#define DECRYPT                     0


/*******************************************************************************
** TYPE 类型定义
*******************************************************************************/
#define IPC_TYPE_ACK                                0x00

/* KEYBOARD */
#define PACKET_TYPE_KEYBOARD                        0x01

/* CARD */
#define PACKET_TYPE_CARD_ICCARD                     0x02
#define PACKET_TYPE_CARD_MAGCARD                    0x03
#define PACKET_TYPE_CARD_NFC                        0x04

/* ALG */
#define PACKET_TYPE_ALG_AES                         0x05
#define PACKET_TYPE_ALG_DES                         0x06
#define PACKET_TYPE_ALG_HASH                        0x07
#define PACKET_TYPE_ALG_RSA                         0x08
#define PACKET_TYPE_ALG_SM2                         0x09
#define PACKET_TYPE_ALG_SM4                         0x0A
#define PACKET_TYPE_ALG_TRNG                        0x0B

/* TAMPER */
#define PACKET_TYPE_TAMPER                          0x0C

/* BUZZER */
#define PACKET_TYPE_BUZZER                          0x0D

/* RTC */
#define PACKET_TYPE_RTC                             0x0E

/* PED */
#define PACKET_TYPE_PED                             0x0F

#define PACKET_TYPE_SAFEKEYBOARD                    0x10

#define PACKET_TYPE_VERSION                         0xFD

#define PACKET_TYPE_END                             0x11

/*******************************************************************************
** SUB TYPE 类型定义
*******************************************************************************/
//PED
typedef enum{
    PED_SUB_TYPE_PED_GET_RANDOM_NR = 0,         //ped_get_random_nr
    PED_SUB_TYPE_PED_GET_FULL_STATUS = 1,       //ped_get_full_status
    PED_SUB_TYPE_PED_SET_PIN_INPUT_REGION = 2,  //ped_set_pin_input_region
    PED_SUB_TYPE_PED_FORMAT = 3,                //ped_format
    PED_SUB_TYPE_PED_DELETE_KEY_UNIFIED = 4,    //ped_delete_key_unified
    PED_SUB_TYPE_PED_GET_SENSITIVE_TIMER = 5,   //ped_get_sensitive_timer
    PED_SUB_TYPE_PED_WRITE_KEY_UNIFIED = 6,     //ped_write_key_unified
    PED_SUB_TYPE_PED_GET_PIN_UNIFIED = 7,       //ped_get_pin_unified
    PED_SUB_TYPE_PED_ICC_GET_SLOTNO = 8,        //ped_icc_get_slotno
    PED_SUB_TYPE_PED_GET_OFFLINE_PIN = 9,       //ped_get_offline_pin
    PED_SUB_TYPE_PED_GET_MAC_UNIFIED = 10,       //ped_get_mac_unified
    PED_SUB_TYPE_PED_INJECT_KEY = 11,            //ped_inject_key
    PED_SUB_TYPE_PED_ROOT_INJECT_KEY = 12,       //ped_root_inject_key
    PED_SUB_TYPE_PED_CHECK_KEY_UNIFIED = 13,     //ped_check_key_unified
    PED_SUB_TYPE_PED_SYMMETRIC_CRYPTO = 14,      //ped_symmetric_crypto
    PED_SUB_TYPE_PED_CRYPTO_UNIFIED = 15,        //ped_crypto_unified
    PED_SUB_TYPE_PED_GET_KEY_KVC = 16,           //ped_get_key_kvc
    PED_SUB_TYPE_PED_RSA_PRIVATE_OPERATION = 17, //ped_rsa_private_operation
    PED_SUB_TYPE_PED_RSA_GENKEY = 18,            //ped_rsa_genkey
    PED_SUB_TYPE_PED_RSA_PUBLIC_BLOCK = 19,      //ped_rsa_public_block
    PED_SUB_TYPE_PED_RSA_PRIVATE_BLOCK = 20,     //ped_rsa_private_block
    PED_SUB_TYPE_PED_RSA_SIGN = 21,              //ped_rsa_sign
    PED_SUB_TYPE_PED_RSA_VERIFY = 22,            //ped_rsa_verify
    PED_SUB_TYPE_PED_GET_MAC_SM4_ANS = 23,       //ped_get_mac_sm4_ans
    PED_SUB_TYPE_PED_INJECT_ANS_KEY = 24,        //ped_inject_ans_key
}PED_SUB_TYPE;

/************************************************************************
 * 
 * KEYBOARD
 *
 * data 串口上报顺序
 * event(1) + [time_ms(2)]
 *
 * event :
 *  - KEY_PRESS   1     time_ms 为 0
 *  - KEY_RELEASE 0     time_ms 为 press 到 release 持续的ms
 *
 * *********************************************************************/
#define KEYBOARD_SUB_TYPE_KEY_0                     (0x30)
#define KEYBOARD_SUB_TYPE_KEY_1                     (0x31)
#define KEYBOARD_SUB_TYPE_KEY_2                     (0x32)
#define KEYBOARD_SUB_TYPE_KEY_3                     (0x33)
#define KEYBOARD_SUB_TYPE_KEY_4                     (0x34)
#define KEYBOARD_SUB_TYPE_KEY_5                     (0x35)
#define KEYBOARD_SUB_TYPE_KEY_6                     (0x36)
#define KEYBOARD_SUB_TYPE_KEY_7                     (0x37)
#define KEYBOARD_SUB_TYPE_KEY_8                     (0x38)
#define KEYBOARD_SUB_TYPE_KEY_9                     (0x39)
#define KEYBOARD_SUB_TYPE_BACKSPACE                 (0x09)
#define KEYBOARD_SUB_TYPE_CLEAR                     (0x2E)
#define KEYBOARD_SUB_TYPE_ALPHA                     (0x07)
#define KEYBOARD_SUB_TYPE_UP                        (0x04)
#define KEYBOARD_SUB_TYPE_DOWN                      (0x05)
#define KEYBOARD_SUB_TYPE_FN                        (0x15)
#define KEYBOARD_SUB_TYPE_MENU                      (0x14)
#define KEYBOARD_SUB_TYPE_ENTER                     (0x08)
#define KEYBOARD_SUB_TYPE_CANCEL                    (0x03)
#define KEYBOARD_SUB_TYPE_PRNUP                     (0x19)
#define KEYBOARD_SUB_TYPE_POWER                     (0x02)
#define KEYBOARD_SUB_TYPE_CAMERA                    (0x0E)
#define KEYBOARD_SUB_TYPE_INVALID                   (0xFF)
#define KEYBOARD_SUB_TYPE_TIMEOUT                   (0x00)
#define KEYBOARD_SUB_TYPE_F1                        (0xF1)
#define KEYBOARD_SUB_TYPE_F2                        (0xF2)

#define KEYBOARD_SUB_TYPE_X                         (0xAA) /* safe keyboard */


//safe keyboard
#define SAFEKEYBOARD_SUB_TYPE_DISABLE               (0)
#define SAFEKEYBOARD_SUB_TYPE_ENABLE                (1)

//AES
#define ALG_AES_SUB_TYPE_ENCRYPT_ECB                (0)
#define ALG_AES_SUB_TYPE_DECRYPT_ECB                (1)
#define ALG_AES_SUB_TYPE_ENCRYPT_CBC                (2)
#define ALG_AES_SUB_TYPE_DECRYPT_CBC                (3)

//DES
#define ALG_DES_SUB_TYPE_ENCRYPT_ECB                (0)
#define ALG_DES_SUB_TYPE_DECRYPT_ECB                (1)
#define ALG_DES_SUB_TYPE_ENCRYPT_CBC                (2)
#define ALG_DES_SUB_TYPE_DECRYPT_CBC                (3)

//HASH
#define ALG_HASH_SUB_TYPE_INIT                      (0)
#define ALG_HASH_SUB_TYPE_UPDATE                    (1)
#define ALG_HASH_SUB_TYPE_FINAL                     (2)

//RSA
#define ALG_RSA_SUB_TYPE_GENERATE_KEY               (0)
#define ALG_RSA_SUB_TYPE_PUBKEY                     (1)//加密、验签
#define ALG_RSA_SUB_TYPE_PRIKEY                     (2)//解密、签名

//SM2
#define ALG_SM2_SUB_TYPE_GENERATE_KEY               (0)
#define ALG_SM2_SUB_TYPE_ENCRYPT                    (1)
#define ALG_SM2_SUB_TYPE_DECRYPT                    (2)
#define ALG_SM2_SUB_TYPE_GET_HASH                   (3)
#define ALG_SM2_SUB_TYPE_SIGN                       (4)
#define ALG_SM2_SUB_TYPE_VERIFY                     (5)

//SM4
#define ALG_SM4_SUB_TYPE_ENCRYPT_ECB                (0)
#define ALG_SM4_SUB_TYPE_DECRYPT_ECB                (1)
#define ALG_SM4_SUB_TYPE_ENCRYPT_CBC                (2)
#define ALG_SM4_SUB_TYPE_DECRYPT_CBC                (3)

//ICCARD
#define CARD_ICCARD_SUB_TYPE_OPEN                   (0)
#define CARD_ICCARD_SUB_TYPE_CLOSE                  (1)
#define CARD_ICCARD_SUB_TYPE_CHECKCARD              (2)
#define CARD_ICCARD_SUB_TYPE_POWERUP                (3)
#define CARD_ICCARD_SUB_TYPE_APDU                   (4)
#define CARD_ICCARD_SUB_TYPE_POWERDOWN              (5)

//MAGCARD
#define CARD_MAGCARD_SUB_TYPE_OPEN                  (0)
#define CARD_MAGCARD_SUB_TYPE_CLOSE                 (1)
#define CARD_MAGCARD_SUB_TYPE_READ                  (2)
#define CARD_MAGCARD_SUB_TYPE_SWIPED                (3)

//NFC
#define CARD_NFC_SUB_TYPE_MIF_OPEN                                        (0)
#define CARD_NFC_SUB_TYPE_MIF_CLOSE                                       (1)
#define CARD_NFC_SUB_TYPE_ISO14443_RESET_PICC                             (2)
#define CARD_NFC_SUB_TYPE_MIF_SELECT_CARRIER_TYPE                         (3)
#define CARD_NFC_SUB_TYPE_ISO14443_WUPB                                   (4)
#define CARD_NFC_SUB_TYPE_ISO14443_TCL_ATTRIB                             (5)
#define CARD_NFC_SUB_TYPE_ISO14443_NO_TCL_EXCHANGE                        (6)
#define CARD_NFC_SUB_TYPE_EMV_CONTACTLESS_ACTIVE_PICC                     (7)
#define CARD_NFC_SUB_TYPE_EMV_CONTACTLESS_DEACTIVE_PICC                   (8)
#define CARD_NFC_SUB_TYPE_EMV_CONTACTLESS_OBTAIN_STATUS                   (9)
#define CARD_NFC_SUB_TYPE_EMV_CONTACTLESS_GET_LASTERROR                   (0x0A)
#define CARD_NFC_SUB_TYPE_EMV_CONTACTLESS_EXCHANGE_APDU                   (0x0B)
#define CARD_NFC_SUB_TYPE_EMV_CONTACTLESS_EXCHANGE_APDU_NONBLOCK_SEND     (0x0C)
#define CARD_NFC_SUB_TYPE_EMV_CONTACTLESS_EXCHANGE_APDU_NONBLOCK_RESPONSE (0x0D)
#define CARD_NFC_SUB_TYPE_EMV_CONTACTLESS_EXCHANGE_APDU_NONBLOCK_ABORT    (0x0E)



//TAMPER
#define TAMPER_SUB_TYPE_CONFIG                      (0)
#define TAMPER_SUB_TYPE_GET_STATUS                  (1)
#define TAMPER_SUB_TYPE_REPORT                      (0)

//BUZZER
#define BUZZER_SUB_TYPE_BEEP                        (0)
#define BUZZER_SUB_TYPE_ENABLE                      (1)
#endif // __IPC_CMD_H__

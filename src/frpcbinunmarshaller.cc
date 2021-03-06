/*
 * FastRPC -- Fast RPC library compatible with XML-RPC
 * Copyright (C) 2005-7  Seznam.cz, a.s.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Seznam.cz, a.s.
 * Radlicka 2, Praha 5, 15000, Czech Republic
 * http://www.seznam.cz, mailto:fastrpc@firma.seznam.cz
 *
 * FILE          $Id: frpcbinunmarshaller.cc,v 1.11 2010-06-10 15:21:04 mirecta Exp $
 *
 * DESCRIPTION
 *
 * AUTHOR
 *              Miroslav Talasek <miroslav.talasek@firma.seznam.cz>
 *
 * HISTORY
 *
 */
#include "frpcbinunmarshaller.h"
#include "frpctreebuilder.h"
#include <memory.h>
#include <frpcstreamerror.h>


#define FRPC_GET_DATA_TYPE( data ) (data >> 3)
#define FRPC_GET_DATA_TYPE_INFO( data ) (data & 0x07 )
namespace FRPC {
namespace {

/** Decodes zigzag encoded unsigned int back to signed int.
 */
int64_t zigzagDecode(int64_t s) {
    uint64_t n = static_cast<uint64_t>(s);
    return static_cast<int64_t>((n >> 1) ^ (-(s & 1)));
}

} // namespace



BinUnMarshaller_t::~BinUnMarshaller_t() {}


void BinUnMarshaller_t::finish() {
    if (internalType !=NONE || entityStorage.size() > 0)
        throw StreamError_t("Stream not complete");

}

void BinUnMarshaller_t::unMarshall(const char *data, unsigned int size, char type) {
    try {
        unMarshallInternal(data, size, type);
    }
    catch (...) {
        mainBuff.finish();
        throw;
    }
    mainBuff.finish();
}

void BinUnMarshaller_t::unMarshallInternal(const char *data, unsigned int size, char type) {
    unsigned char magic[]={0xCA, 0x11};

    while (true) {
        if (readData(data, size) != 0 )
            return;

        switch (internalType) {
        case MAGIC: {
            if (memcmp(mainBuff.data(), magic, 2) != 0) {
                //bad magic
                throw StreamError_t("Bad magic !!!");
            }
            protocolVersion.versionMajor = mainBuff.data()[2];
            protocolVersion.versionMinor = mainBuff.data()[3];
            if (protocolVersion.versionMajor > 3) {
                //unsupeorted protocol version
                throw StreamError_t("Unsupported protocol version !!!");
            }

            mainBuff.erase();
            internalType = MAIN;

            dataWanted = 1;
        }
        break;

        case MAIN: {
            mainInternalType =  FRPC_GET_DATA_TYPE(mainBuff[0]);
            mainBuff.erase();

            if (mainInternalType != type && type != TYPE_ANY ) {

                if (mainInternalType != FAULT || type != TYPE_METHOD_RESPONSE) {
                    throw StreamError_t("Bad main Type !!!");
                }
            }

            if (mainInternalType == METHOD_CALL)
                internalType = METHOD_NAME_LEN;
            else
                internalType = NONE;

            if (mainInternalType == METHOD_RESPONSE) {
                dataBuilder.buildMethodResponse();
                mainInternalType = NONE;
            }
            dataWanted = 1;
        }
        break;
        case METHOD_NAME_LEN: {
            dataWanted = static_cast<unsigned char>(mainBuff[0]);
            internalType = METHOD_NAME;
            mainBuff.erase();
        }
        break;
        case METHOD_NAME: {
            dataBuilder.buildMethodCall(mainBuff.data(), mainBuff.size());
            mainBuff.erase();
            mainInternalType = NONE;
            internalType = NONE;
            dataWanted = 1;
        }
        break;
        case MEMBER_NAME: {
            dataBuilder.buildStructMember(mainBuff.data(), mainBuff.size());
#ifdef _DEBUG
            printf( "struct member: %s \n", std::string(mainBuff.data(), mainBuff.size()).c_str());
#endif
            //we have member name
            entityStorage.back().member = true;

            mainBuff.erase();
            internalType = NONE;
            dataWanted = 1;
        }
        break;
        case NONE: {
            if (isNextMember()) {
                dataWanted = static_cast<unsigned char>(mainBuff[0]);
                mainBuff.erase();
                internalType = MEMBER_NAME;
            } else {
                switch (FRPC_GET_DATA_TYPE(mainBuff[0])) {
                case BOOL: {
                    dataBuilder.buildBool(FRPC_GET_DATA_TYPE_INFO(mainBuff[0]) & 0x01);
                    //decrement member count
#ifdef _DEBUG
		    printf("bool\n");
#endif
                    internalType = NONE;
                    mainBuff.erase();
                    dataWanted = 1;
                    decrementMember();
                }
                break;
                case NULLTYPE: {
                    TreeBuilder_t *treeBuilder(dynamic_cast<TreeBuilder_t*>(&dataBuilder));
                    if (treeBuilder) {
                        treeBuilder->buildNull();
                    } else {
                        dynamic_cast<DataBuilderWithNull_t&>(dataBuilder).buildNull();
                    }
                    internalType = NONE;
                    mainBuff.erase();
                    dataWanted = 1;
                    decrementMember();
                }
                break;
                case INT: {
                    internalType = INT;
                    if (protocolVersion.versionMajor > 2) {
                        dataWanted = FRPC_GET_DATA_TYPE_INFO(mainBuff[0]) + 1;
                    } else {
                        dataWanted = FRPC_GET_DATA_TYPE_INFO(mainBuff[0]);
                        if (dataWanted > 4 || !dataWanted)
                            throw StreamError_t("Size of int is 0 or > 4 !!!");
                    }
                    mainBuff.erase();
#ifdef _DEBUG
		    printf("int lenght:%d\n",dataWanted);
#endif
                }
                break;
                case INTN8: {
                    internalType = INTN8;
                    dataWanted = FRPC_GET_DATA_TYPE_INFO(mainBuff[0]) + 1;
                    mainBuff.erase();
                }
                break;
                case INTP8: {
                    internalType = INTP8;
                    dataWanted = FRPC_GET_DATA_TYPE_INFO(mainBuff[0]) + 1;
                    mainBuff.erase();
                }
                break;
                case DOUBLE: {
                    internalType = DOUBLE;
                    dataWanted = 8;
                    mainBuff.erase();
#ifdef _DEBUG
                    printf("double size 8 \n");
#endif
                }
                break;
                case DATETIME: {
                    internalType = DATETIME;
                    if (protocolVersion.versionMajor > 2) {
                        dataWanted = 14;
                    } else {
                        dataWanted = 10;
                    }
                    mainBuff.erase();
#ifdef _DEBUG
                    printf("datetime size 10 \n");
#endif
                }
                break;

                case STRING: {
                    internalType = STRING;
                    typeEvent = LENGTH;
                    if (protocolVersion.versionMajor < 2)
                        dataWanted = FRPC_GET_DATA_TYPE_INFO(mainBuff[0]);
                    else
                        dataWanted = FRPC_GET_DATA_TYPE_INFO(mainBuff[0]) + 1;

                    if (!dataWanted)
                        throw StreamError_t("Size of string length is 0 !!!");
                    mainBuff.erase();
#ifdef _DEBUG
                    printf("string size size: %d (or + 1) \n",dataWanted);
#endif
                }
                break;

                case BINARY: {
                    internalType = BINARY;
                    typeEvent = LENGTH;
                    if (protocolVersion.versionMajor < 2)
                        dataWanted = FRPC_GET_DATA_TYPE_INFO(mainBuff[0]);
                    else
                        dataWanted = FRPC_GET_DATA_TYPE_INFO(mainBuff[0]) + 1;

                    if (!dataWanted)
                        throw StreamError_t("Size of binary length is 0 !!!");
                    mainBuff.erase();
#ifdef _DEBUG
                    printf("binary size size: %d (or + 1) \n",dataWanted);
#endif

                }
                break;
                case ARRAY: {
                    internalType = ARRAY;
                    if (protocolVersion.versionMajor < 2)
                        dataWanted = FRPC_GET_DATA_TYPE_INFO(mainBuff[0]);
                    else
                        dataWanted = FRPC_GET_DATA_TYPE_INFO(mainBuff[0]) + 1;

                    mainBuff.erase();
#ifdef _DEBUG
                    printf("array size size: %d (or + 1) \n",dataWanted);
#endif
                }
                break;
                case STRUCT: {
                    internalType = STRUCT;
                    if (protocolVersion.versionMajor < 2)
                        dataWanted = FRPC_GET_DATA_TYPE_INFO(mainBuff[0]);
                    else
                        dataWanted = FRPC_GET_DATA_TYPE_INFO(mainBuff[0]) + 1;

                    mainBuff.erase();
#ifdef _DEBUG
                    printf("struct size size: %d (or + 1) \n",dataWanted);
#endif

                }
                break;
                default:
                    throw StreamError_t("Don't known this type");
                    break;

                }

            }

        }
        break;
        //Types
        case STRING: {
            if (typeEvent == LENGTH) {
                //unpack string len
                Number_t stringSize(mainBuff.data(), mainBuff.size());
                dataWanted = stringSize.number;
#ifdef _DEBUG
                    printf("string  size: %d  \n",dataWanted);
#endif

                mainBuff.erase();
                if (!dataWanted) {
                    if (mainInternalType == FAULT) {
                        dataBuilder.buildFault(errNo, mainBuff.data(), mainBuff.size());
                        mainInternalType = NONE;
                        internalType = NONE;

                    } else {
                        dataBuilder.buildString(mainBuff.data(), mainBuff.size());
                        internalType = NONE;
                        dataWanted = 1;
                        decrementMember();
                    }
                } else {
                    typeEvent = DATA;
                }
            } else {
                //obtain whole string
                if (mainInternalType == FAULT) {
                    dataBuilder.buildFault(errNo, mainBuff.data(), mainBuff.size());
                    mainInternalType = NONE;
                    internalType = NONE;
                    mainBuff.erase();
                } else {
                    dataBuilder.buildString(mainBuff.data(), mainBuff.size());
                    internalType = NONE;
                    dataWanted = 1;
                    decrementMember();
                    mainBuff.erase();
                }
            }
        }
        break;
        case BINARY: {
            if (typeEvent == LENGTH) {
                //unpack string len
                Number_t stringSize(mainBuff.data(), mainBuff.size());
                dataWanted = stringSize.number;
                mainBuff.erase();
                if (!dataWanted) {
                    dataBuilder.buildBinary(mainBuff.data(), mainBuff.size());
                    internalType = NONE;
                    dataWanted = 1;
                    decrementMember();
                } else {
                    typeEvent = DATA;
                }
#ifdef _DEBUG
                printf( "binary size: %d \n",dataWanted);
#endif

            } else {
                //obtain whole data
                dataBuilder.buildBinary(mainBuff.data(), mainBuff.size());
                internalType = NONE;
                dataWanted = 1;
                decrementMember();
                mainBuff.erase();
            }


        }
        break;
        case INT: {
            if (protocolVersion.versionMajor > 2) {
                Number_t value(mainBuff.data(), mainBuff.size());
                value.number = zigzagDecode(value.number);
                if (mainInternalType == FAULT)
                    errNo = value.number;
                else
                    dataBuilder.buildInt(value.number);
            } else {
                //unpack value
                Number32_t value(mainBuff.data(), mainBuff.size());
                if (mainInternalType == FAULT)
                    errNo = value.number;
                else
                    dataBuilder.buildInt(value.number);
            }
            internalType = NONE;
            dataWanted = 1;
            //decrement member count
            decrementMember();
            mainBuff.erase();
#ifdef _DEBUG
                printf( "int number: %i \n",value.number);
#endif

        }
        break;

        case INTN8: {
            //unpack value
            Number_t value(mainBuff.data(), mainBuff.size());
            if (mainInternalType == FAULT)
                errNo = -value.number;
            else
                dataBuilder.buildInt(-value.number);

            internalType = NONE;
            dataWanted = 1;
            //decrement member count
            decrementMember();
            mainBuff.erase();
        }
        break;

        case INTP8: {
            //unpack value
            Number_t value(mainBuff.data(), mainBuff.size());
            if (mainInternalType == FAULT)
                errNo = value.number;
            else
                dataBuilder.buildInt(value.number);

            internalType = NONE;
            dataWanted = 1;
            //decrement member count
            decrementMember();
            mainBuff.erase();
        }
        break;

        case DOUBLE: {
            double value;
             //write data
            char data[8];
            memset(data, 0, 8);
            memcpy(data, mainBuff.data(), 8);

#ifdef FRPC_BIG_ENDIAN
           //swap it
           SWAP_BYTE(data[7],data[0]);
           SWAP_BYTE(data[6],data[1]);
           SWAP_BYTE(data[5],data[2]);
           SWAP_BYTE(data[4],data[3]);
#endif

            memcpy((char*)&value,data,8);

            //call builder
            dataBuilder.buildDouble(value);
            internalType = NONE;
            dataWanted = 1;
            //decrement member count
            decrementMember();
            mainBuff.erase();
        }
        break;
        case DATETIME: {
            if (protocolVersion.versionMajor > 2) {
                DateTimeDataV3_t dateTime;

                memcpy(dateTime.data, mainBuff.data(), 14);
                //unpack
                dateTime.unpack();

                if (dateTime.dateTime.year || dateTime.dateTime.month
                    || dateTime.dateTime.day || dateTime.dateTime.hour
                    || dateTime.dateTime.minute || dateTime.dateTime.sec)
                {
                    //call builder
                    dataBuilder.buildDateTime(dateTime.dateTime.year + 1600,
                                              dateTime.dateTime.month,
                                              dateTime.dateTime.day,
                                              dateTime.dateTime.hour,
                                              dateTime.dateTime.minute,
                                              dateTime.dateTime.sec,
                                              dateTime.dateTime.weekDay,
                                              dateTime.dateTime.unixTime,
                                              dateTime.dateTime.timeZone * 15 * 60);
                } else {
                    dataBuilder.buildDateTime(dateTime.dateTime.year,
                                              dateTime.dateTime.month,
                                              dateTime.dateTime.day,
                                              dateTime.dateTime.hour,
                                              dateTime.dateTime.minute,
                                              dateTime.dateTime.sec,
                                              dateTime.dateTime.weekDay,
                                              dateTime.dateTime.unixTime,
                                              dateTime.dateTime.timeZone * 15 * 60);
                }
            } else { // protocol v 2.x or earlier
                DateTimeData_t dateTime;

                memcpy(dateTime.data, mainBuff.data(), 10);
                //unpack
                dateTime.unpack();

                if (dateTime.dateTime.year || dateTime.dateTime.month
                    || dateTime.dateTime.day || dateTime.dateTime.hour
                    || dateTime.dateTime.minute || dateTime.dateTime.sec) {

                    //call builder
                    dataBuilder.buildDateTime(dateTime.dateTime.year + 1600,
                                              dateTime.dateTime.month,
                                              dateTime.dateTime.day,
                                              dateTime.dateTime.hour,
                                              dateTime.dateTime.minute,
                                              dateTime.dateTime.sec,
                                              dateTime.dateTime.weekDay,
                                              dateTime.dateTime.unixTime,
                                              dateTime.dateTime.timeZone * 15 * 60);
                } else {
                    dataBuilder.buildDateTime(dateTime.dateTime.year,
                                              dateTime.dateTime.month,
                                              dateTime.dateTime.day,
                                              dateTime.dateTime.hour,
                                              dateTime.dateTime.minute,
                                              dateTime.dateTime.sec,
                                              dateTime.dateTime.weekDay,
                                              dateTime.dateTime.unixTime,
                                              dateTime.dateTime.timeZone * 15 * 60);
                }
            }
            internalType = NONE;
            dataWanted = 1;
            //decrement member count
            decrementMember();
            mainBuff.erase();

        }
        break;
        case STRUCT: {
            Number_t numOfMembers(mainBuff.data(), mainBuff.size());

            //call builder
            dataBuilder.openStruct(numOfMembers.number);
            if (numOfMembers.number != 0) {
                //save event
                entityStorage.push_back(TypeStorage_t(STRUCT, numOfMembers.number));
            } else {
                dataBuilder.closeStruct();
                decrementMember();
            }
            internalType = NONE;
            dataWanted = 1;
            mainBuff.erase();
#ifdef _DEBUG
            printf( "struct size: %i \n",numOfMembers.number);
#endif
        }
        break;
        case ARRAY: {
            //unpack number
            Number_t numOfItems(mainBuff.data(), mainBuff.size());

            //call builder
            dataBuilder.openArray(numOfItems.number);
            //save evnt
            if (numOfItems.number != 0) {
                entityStorage.push_back(TypeStorage_t(ARRAY, numOfItems.number));
            } else {
                dataBuilder.closeArray();
                decrementMember();
            }
            internalType = NONE;
            dataWanted = 1;
            mainBuff.erase();
#ifdef _DEBUG
            printf( "array size: %i \n",numOfItems.number);
#endif

        }
        break;

        }
    }


}

}

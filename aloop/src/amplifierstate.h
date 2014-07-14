/*
 * amplifierstate.h
 *
 *  Created on: Jul 14, 2014
 *      Author: artem
 */

#ifndef AMPLIFIERSTATE_H_
#define AMPLIFIERSTATE_H_

#include <stdint.h>

typedef struct
{
	int32_t mInputLeftLevelMeter;
	int32_t mInputRightLevelMeter;

}	AmplifierState_t;

typedef struct
{
	int32_t mTag;
	int32_t mRevision;
	int32_t mId;
	int32_t mLength;

}	AmplifierHeader_t;

typedef struct
{
	AmplifierHeader_t mHeader;
	AmplifierState_t mState;

}	AmplifierPacket_t;

#endif /* AMPLIFIERSTATE_H_ */

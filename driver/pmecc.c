/* ----------------------------------------------------------------------------
 *         ATMEL Microcontroller Software Support
 * ----------------------------------------------------------------------------
 * Copyright (c) 2013, Atmel Corporation
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Atmel's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "common.h"
#include "hardware.h"
#include "board.h"
#include "arch/at91_nand_ecc.h"
#include "nand.h"
#include "pmecc.h"
#include "debug.h"
#include "div.h"

static int pmecc_readl(unsigned int reg)
{
	return readl(AT91C_BASE_PMECC + reg);
}

static void pmecc_writel(unsigned int value, unsigned reg)
{
	writel(value, (AT91C_BASE_PMECC + reg));
}

static int pmecclor_readl(unsigned int reg)
{
	return readl(AT91C_BASE_PMERRLOC + reg);
}

static void pmecclor_writel(unsigned int value, unsigned reg)
{
	writel(value, (AT91C_BASE_PMERRLOC + reg));
}

/*
 * Return 1 means valid pmecc error bits & sector size. otherwise return 0;
 */
static int is_valid_pmecc_params()
{
	int ret = 1;
	switch (PMECC_ERROR_CORR_BITS) {
	case 2:
	case 4:
	case 8:
	case 12:
	case 24:
		break;
	default:
		dbg_info("Invalid Pmecc error bits: %d. Should " \
			"be 2, 4, 8, 12 or 24.\n", PMECC_ERROR_CORR_BITS);
		ret = 0;
	}

	if (PMECC_SECTOR_SIZE != 512 && PMECC_SECTOR_SIZE != 1024) {
		dbg_info("Invalid Pmecc sector size: %d. Should " \
				"be 512 or 1024.\n", PMECC_SECTOR_SIZE);
		ret = 0;
	}

	return ret;
}

/*
 * Return number of ecc bytes per sector according to sector size and
 * correction capability
 *
 * Following table shows what at91 PMECC supported:
 * Correction Capability	Sector_512_bytes	Sector_1024_bytes
 * =====================	================	=================
 *                2-bits                 4-bytes                  4-bytes
 *                4-bits                 7-bytes                  7-bytes
 *                8-bits                13-bytes                 14-bytes
 *               12-bits                20-bytes                 21-bytes
 *               24-bits                39-bytes                 42-bytes
 */
int get_pmecc_bytes()
{
	int i;
	int error_corr_bits[] =		{2, 4, 8,  12, 24};
	int ecc_bytes_sec_512[] =	{4, 7, 13, 20, 39};
	int ecc_bytes_sec_1024[] =	{4, 7, 14, 21, 42};

	int ecc_bytes = 0;
	for (i = 0; i < 5; i++) {
		if (error_corr_bits[i] == PMECC_ERROR_CORR_BITS) {
			/* find out the index */
			ecc_bytes = (PMECC_SECTOR_SIZE == 512) ?
				ecc_bytes_sec_512[i] : ecc_bytes_sec_1024[i];
			break;
		}
	}

	return ecc_bytes;	/* 0 indicate not found */
}

static int init_pmecc_descripter(struct _PMECC_paramDesc_struct *pmecc_params,
				struct nand_info *nand)
{
	unsigned int sectors;

	if ((nand->pagesize == 2048) || (nand->pagesize == 4096)) {
		/* Sector Size */
		pmecc_params->sectorSize = (PMECC_SECTOR_SIZE == 512) ?
			AT91C_PMECC_SECTORSZ_512 : AT91C_PMECC_SECTORSZ_1024;

		pmecc_params->nandWR = AT91C_PMECC_NANDWR_0;
		pmecc_params->spareEna = AT91C_PMECC_SPAREENA_DIS;
		pmecc_params->modeAuto = AT91C_PMECC_AUTO_DIS;

		pmecc_params->spareSize = nand->oobsize;
		pmecc_params->eccSizeByte = nand->ecclayout->eccbytes;
		pmecc_params->eccStartAddress = nand->ecclayout->eccpos[0];
		pmecc_params->eccEndAddress
		= nand->ecclayout->eccpos[nand->ecclayout->eccbytes - 1];

		/* At 133Mhz, this field must be programmed with 2 */
		pmecc_params->clkCtrl = 2;

		pmecc_params->interrupt = 0;
		pmecc_params->tt = PMECC_ERROR_CORR_BITS;
		pmecc_params->mm = (PMECC_SECTOR_SIZE == 512) ? 13 : 14;
		pmecc_params->nn = (1 << pmecc_params->mm) - 1;

		if (PMECC_SECTOR_SIZE == 512) {
			pmecc_params->alpha_to = (short *)(AT91C_BASE_ROM
					+ CONFIG_LOOKUP_TABLE_ALPHA_OFFSET);
			pmecc_params->index_of = (short *)(AT91C_BASE_ROM
					+ CONFIG_LOOKUP_TABLE_INDEX_OFFSET);
		} else {
			pmecc_params->alpha_to = (short *)(AT91C_BASE_ROM
				+ CONFIG_LOOKUP_TABLE_ALPHA_OFFSET_1024);
			pmecc_params->index_of = (short *)(AT91C_BASE_ROM
				+ CONFIG_LOOKUP_TABLE_INDEX_OFFSET_1024);
		}

		/* Error Correct Capability */
		switch (PMECC_ERROR_CORR_BITS) {
		case 2:
			pmecc_params->errBitNbrCapability
						= AT91C_PMECC_BCH_ERR2;
			break;
		case 4:
			pmecc_params->errBitNbrCapability
						= AT91C_PMECC_BCH_ERR4;
			break;
		case 8:
			pmecc_params->errBitNbrCapability
						= AT91C_PMECC_BCH_ERR8;
			break;
		case 12:
			pmecc_params->errBitNbrCapability
						= AT91C_PMECC_BCH_ERR12;
			break;
		case 24:
			pmecc_params->errBitNbrCapability
						= AT91C_PMECC_BCH_ERR24;
			break;
		default:
			dbg_info("PMECC: Invalid error correctable " \
				"bits: %d\n",	PMECC_ERROR_CORR_BITS);
			return -1;
		}

		/* Number of Sectors in the Page */
		sectors = div(nand->pagesize, PMECC_SECTOR_SIZE);
		if (sectors == 1) {
			pmecc_params->pageSize = AT91C_PMECC_PAGESIZE_1SEC;
		} else if (sectors == 2) {
			pmecc_params->pageSize = AT91C_PMECC_PAGESIZE_2SEC;
		} else if (sectors == 4) {
			pmecc_params->pageSize = AT91C_PMECC_PAGESIZE_4SEC;
		} else if (sectors == 8) {
			pmecc_params->pageSize = AT91C_PMECC_PAGESIZE_8SEC;
		} else {
			dbg_info("PMECC: Not supported sector size: %d\n",
				sectors);
			return -1;
		}

		dbg_loud("PMECC: page_size: %u, oob_size: %u, " \
					"pmecc_cap: %u, sector_size: %u\n",
			nand->pagesize, nand->oobsize, pmecc_params->tt,
			pmecc_params->sectorSize
				== AT91C_PMECC_SECTORSZ_512 ? 512 : 1024);

		return 0;
	} else {
		dbg_info("PMECC: Not supported page size: %d\n",
							nand->pagesize);
		return -1;
	}
}

static int init_pmecc_core(struct _PMECC_paramDesc_struct *pmecc_params)
{
	pmecc_writel(AT91C_PMECC_DISABLE, PMECC_CTRL);
	pmecc_writel(AT91C_PMECC_RST, PMECC_CTRL);

	pmecc_writel(pmecc_params->errBitNbrCapability |
		pmecc_params->sectorSize |
		pmecc_params->pageSize |
		pmecc_params->nandWR |
		pmecc_params->spareEna |
		pmecc_params->modeAuto, PMECC_CFG);

	pmecc_writel((pmecc_params->spareSize - 1), PMECC_SAREA);
	pmecc_writel(pmecc_params->eccStartAddress, PMECC_SADDR);
	pmecc_writel(pmecc_params->eccEndAddress, PMECC_EADDR);
	pmecc_writel(pmecc_params->clkCtrl, PMECC_CLK);
	pmecc_writel(0xff, PMECC_IDR);

	pmecc_writel(AT91C_PMECC_ENABLE, PMECC_CTRL);

	return 0;
}

int init_pmecc(struct nand_info *nand)
{
	/* sanity check for the pmecc sector size and error bits */
	if (!is_valid_pmecc_params())
		return -1;

	if (init_pmecc_descripter(&PMECC_paramDesc, nand) != 0)
		return -1;

	dbg_info("NAND: Initialize PMECC params, cap: %d, sector: %d\n",
			PMECC_ERROR_CORR_BITS, PMECC_SECTOR_SIZE);

	init_pmecc_core(&PMECC_paramDesc);

	return 0;
}

void pmecc_enable(void)
{
	pmecc_writel(AT91C_PMECC_RST, PMECC_CTRL);
	pmecc_writel((pmecc_readl(PMECC_CFG) | AT91C_PMECC_AUTO_ENA),
			PMECC_CFG);
	pmecc_writel(AT91C_PMECC_ENABLE, PMECC_CTRL);
}

void pmecc_start_data_phase(void)
{
	pmecc_writel(AT91C_PMECC_RST, PMECC_CTRL);
	pmecc_writel(AT91C_PMECC_ENABLE | AT91C_PMECC_DATA, PMECC_CTRL);
}

static int check_pmecc_ecc_data(struct nand_info *nand,
				unsigned char *buffer)
{
	unsigned int i;
	unsigned char *ecc_data = buffer + nand->pagesize
				+ nand->ecclayout->eccpos[0];

	for (i = 0; i < nand->ecclayout->eccbytes; i++)
		if (*ecc_data++ != 0xff)
			break;

	if (i >= nand->ecclayout->eccbytes)
		return -1;
	else
		return 0;
}

/*
 * \brief Build the pseudo syndromes table
 * \param pPmeccDescriptor Pointer to a PMECC_paramDesc instance.
 * \param sector Targetted sector.
 */

static void GenSyn(unsigned long pPMECC,
		struct _PMECC_paramDesc_struct *pPmeccDescriptor,
		unsigned int sector)
{
	short *pRemainer;
	unsigned int index;

	pRemainer = (short *) (pPMECC + PMECC_REM + (sector * 0x40));

	for (index = 0; index < pPmeccDescriptor->tt; index++)
		/* Fill odd syndromes */
		pPmeccDescriptor->partialSyn[1 +  (2 * index)]
						= pRemainer[index];
}

/**
 * \brief The substitute function evaluates the polynomial remainder,
 * with different values of the field primitive elements.
 * \param pPmeccDescriptor Pointer to a PMECC_paramDesc instance.
 */
static int substitute(struct _PMECC_paramDesc_struct *pPmeccDescriptor)
{
	int i, j;
	short *si;
	short *pPartialSyn = pPmeccDescriptor->partialSyn;
	short *alpha_to = pPmeccDescriptor->alpha_to;
	short *index_of = pPmeccDescriptor->index_of;

	/*
	 * si[] is a table that holds the current syndrome value,
	 * an element of that table belongs to the field.
	 */
	si = pPmeccDescriptor->si;

	for (i = 1; i < 2 * TT_MAX; i++)
		si[i] = 0;

	/* Computation 2t syndromes based on S(x) */
	/* Odd syndromes */
	for (i = 1; i <= 2 * pPmeccDescriptor->tt - 1; i = i + 2) {
		si[i] = 0;
		for (j = 0; j < pPmeccDescriptor->mm; j++) {
			if (pPartialSyn[i] & ((unsigned short)0x1 << j))
				si[i] = alpha_to[(i * j)] ^ si[i];
		}
	}
	/* Even syndrome = (Odd syndrome) ** 2 */
	for (i = 2; i <= 2 * pPmeccDescriptor->tt; i = i + 2) {
		j = i / 2;
		if (si[j] == 0)
			si[i] = 0;
		else
			si[i] = alpha_to[mod((2 * index_of[si[j]]),
				(unsigned int)pPmeccDescriptor->nn)];
	}

	return 0;
}

/*
 * \brief The substitute function finding the value of the error
 * location polynomial.
 * \param pPmeccDescriptor Pointer to a PMECC_paramDesc instance.
 */
static unsigned int get_sigma(struct _PMECC_paramDesc_struct *pPmeccDescriptor)
{
	unsigned int dmu_0_count;
	int i, j, k;
	short *lmu = pPmeccDescriptor->lmu;
	short *si = pPmeccDescriptor->si;
	short tt = pPmeccDescriptor->tt;

	/* mu  */
	int mu[TT_MAX+1];

	/* discrepancy */
	int dmu[TT_MAX+1];

	/* delta order   */
	int delta[TT_MAX+1];

	/* index of largest delta */
	int ro;
	int largest;
	int diff;

	dmu_0_count = 0;

	/* First Row  */

	/* Mu */
	mu[0]  = -1;
	/* Actually -1/2 */
	/* Sigma(x) set to 1 */

	for (i = 0; i < (2 * TT_MAX + 1); i++)
		pPmeccDescriptor->smu[0][i] = 0;

	pPmeccDescriptor->smu[0][0] = 1;

	/* discrepancy set to 1 */
	dmu[0] = 1;

	/* polynom order set to 0 */
	lmu[0] = 0;

	/* delta set to -1 */
	delta[0]  = (mu[0] * 2 - lmu[0]) >> 1;

	/*                     */
	/*     Second Row      */
	/*                     */

	/* Mu */
	mu[1]  = 0;

	/* Sigma(x) set to 1 */
	for (i = 0; i < (2 * TT_MAX + 1); i++)
		pPmeccDescriptor->smu[1][i] = 0;

	pPmeccDescriptor->smu[1][0] = 1;

	/* discrepancy set to S1 */
	dmu[1] = si[1];

	/* polynom order set to 0 */
	lmu[1] = 0;

	/* delta set to 0 */
	delta[1]  = (mu[1] * 2 - lmu[1]) >> 1;

	/* Init the Sigma(x) last row */
	for (i = 0; i < (2 * TT_MAX + 1); i++)
		pPmeccDescriptor->smu[tt + 1][i] = 0;

	for (i = 1; i <= tt; i++) {
		mu[i+1] = i << 1;
		/* Compute Sigma (Mu+1)             */
		/* And L(mu)                        */
		/* check if discrepancy is set to 0 */
		if (dmu[i] == 0) {
			dmu_0_count++;
			if ((tt - (lmu[i] >> 1) - 1) & 0x1) {
				if (dmu_0_count
					== ((tt - (lmu[i] >> 1) - 1) / 2) + 2) {
					for (j = 0; j <= (lmu[i] >> 1) + 1; j++)
						pPmeccDescriptor->smu[tt+1][j]
						= pPmeccDescriptor->smu[i][j];

					lmu[tt + 1] = lmu[i];
					return 0;
				}
			} else {
				if (dmu_0_count
					== ((tt - (lmu[i] >> 1) - 1) / 2) + 1) {
					for (j = 0; j <= (lmu[i] >> 1) + 1; j++)
						pPmeccDescriptor->smu[tt + 1][j]
						= pPmeccDescriptor->smu[i][j];

					lmu[tt + 1] = lmu[i];
					return 0;
				}
			}

			/* copy polynom */
			for (j = 0; j <= lmu[i] >> 1; j++)
				pPmeccDescriptor->smu[i + 1][j]
						= pPmeccDescriptor->smu[i][j];

			/* copy previous polynom order to the next */
			lmu[i + 1] = lmu[i];
		} else {
			ro = 0;
			largest = -1;
			/* find largest delta with dmu != 0 */
			for (j = 0; j < i; j++) {
				if (dmu[j]) {
					if (delta[j] > largest) {
						largest = delta[j];
						ro = j;
					}
				}
			}

			/* compute difference */
			diff = (mu[i] - mu[ro]);

			/* Compute degree of the new smu polynomial */
			if ((lmu[i]>>1) > ((lmu[ro]>>1) + diff))
				lmu[i + 1] = lmu[i];
			else
				lmu[i + 1] = ((lmu[ro]>>1) + diff) * 2;

			/* Init smu[i+1] with 0 */
			for (k = 0; k < (2 * TT_MAX+1); k++)
				pPmeccDescriptor->smu[i+1][k] = 0;

			/* Compute smu[i+1] */
			for (k = 0; k <= lmu[ro]>>1; k++)
				if (pPmeccDescriptor->smu[ro][k] && dmu[i])
					pPmeccDescriptor->smu[i + 1][k + diff] = pPmeccDescriptor->alpha_to[mod((pPmeccDescriptor->index_of[dmu[i]]
						+ (pPmeccDescriptor->nn	- pPmeccDescriptor->index_of[dmu[ro]])
						+ pPmeccDescriptor->index_of[pPmeccDescriptor->smu[ro][k]]), (unsigned int)pPmeccDescriptor->nn)];

			for (k = 0; k <= lmu[i]>>1; k++)
				pPmeccDescriptor->smu[i+1][k] ^= pPmeccDescriptor->smu[i][k];
		}

		/*************************************************/
		/*                                               */
		/*      End Compute Sigma (Mu+1)                 */
		/*      And L(mu)                                */
		/*************************************************/
		/* In either case compute delta */
		delta[i + 1]  = (mu[i + 1] * 2 - lmu[i + 1]) >> 1;

		/* Do not compute discrepancy for the last iteration */
		if (i < tt) {
			for (k = 0 ; k <= (lmu[i + 1] >> 1); k++) {
				if (k == 0)
					dmu[i + 1] = si[2 * (i - 1) + 3];
				/*
				 * check if one operand of the multiplier
				 * is null, its index is -1
				 */
				else if (pPmeccDescriptor->smu[i+1][k] && si[2 * (i - 1) + 3 - k])
					dmu[i + 1] = pPmeccDescriptor->alpha_to[mod((pPmeccDescriptor->index_of[pPmeccDescriptor->smu[i + 1][k]]
							+ pPmeccDescriptor->index_of[si[2 * (i - 1) + 3 - k]]), (unsigned int)pPmeccDescriptor->nn)] ^ dmu[i + 1];
			}
		}
	}
	return 0;
}

/*
 * \brief Init the PMECC Error Location peripheral and start the error
 *        location processing
 * \param pPmeccDescriptor Pointer to a PMECC_paramDesc instance.
 * \param SectorSizeInBits Size of the sector in bits.
 * \return Number of errors
 */
static int ErrorLocation(unsigned long pPMERRLOC,
		struct _PMECC_paramDesc_struct *pPmeccDescriptor,
		unsigned int SectorSizeInBits)
{
	unsigned int alphax;
	unsigned int *pSigma;
	unsigned int errorNumber;
	unsigned int NbrOfRoots;

	/* Disable PMECC Error Location IP */
	pmecclor_writel(0xFFFFFFFF, PMERRLOC_ELDIS);

	errorNumber = 0;
	alphax = 0;

	pSigma = (unsigned int *) (pPMERRLOC + PMERRLOC_SIGMA0);

	for (alphax = 0;
		alphax <= pPmeccDescriptor->lmu[pPmeccDescriptor->tt + 1] >> 1;
		alphax++) {
		*pSigma++ = pPmeccDescriptor->smu[pPmeccDescriptor->tt + 1][alphax];
		errorNumber++;
	}

	pmecclor_writel(((errorNumber - 1) << 16)
			| pmecclor_readl(PMERRLOC_ELCFG), PMERRLOC_ELCFG);
	/* Enable error location process */
	pmecclor_writel(SectorSizeInBits, PMERRLOC_ELEN);

	while ((pmecclor_readl(PMERRLOC_ELISR) & PMERRLOC_ELISR_DONE) == 0)
		;

	NbrOfRoots = (pmecclor_readl(PMERRLOC_ELISR)
				& PMERRLOC_ELISR_ERR_CNT) >> 8;
	/* Number of roots == degree of smu hence <= tt */
	if (NbrOfRoots == pPmeccDescriptor->lmu[pPmeccDescriptor->tt + 1] >> 1)
		return errorNumber - 1;

	/* Number of roots not match the degree of smu ==> unable to correct error */
	return -1;
}

/*
 * \brief Correct errors indicated in the PMECCEL error location registers.
 * \param sectorBaseAddress Base address of the sector.
 * \param ExtraBytes Number of extra bytes of the sector.
 * (encoded Spare Area, only for the last sector)
 * \param ErrorNbr Number of error to correct
 * \return Number of errors
 */
static unsigned int ErrorCorrection(unsigned long pPMERRLOC,
			struct _PMECC_paramDesc_struct *pPmeccDescriptor,
			unsigned int sectorBaseAddress,
			unsigned int eccBaseAddress,
			unsigned int ExtraBytes,
			unsigned int ErrorNbr)
{
	unsigned int *pErrPos;
	unsigned int bytePos;
	unsigned int bitPos;
	unsigned int sectorSize = PMECC_SECTOR_SIZE;

	pErrPos = (unsigned int *)(pPMERRLOC + PMERRLOC_EL0);

	while (ErrorNbr) {
		bytePos = (*pErrPos - 1) / 8;
		bitPos = (*pErrPos - 1) % 8;
		unsigned char *errByte;

		if (bytePos < sectorSize) {
			/* If error is located in the data area(not in ECC) */
			errByte = (unsigned char *)(sectorBaseAddress + bytePos);
			dbg_info("Correct error bit @[#Byte %u,Bit# %u] " \
				"%u -> %u\n",
				(unsigned int)bytePos,
				(unsigned int)bitPos,
				*errByte,
				*errByte ^ (1 << bitPos));
			*errByte ^= (1 << bitPos);
		} else {
			/* error is located in oob area */
			errByte = (unsigned char *)(eccBaseAddress
					+ (bytePos - sectorSize));
			dbg_info("Correct error bit in OOB @[#Byte %u,Bit# %u]" \
				" %u -> %u\n",
				(unsigned int)bytePos - sectorSize,
				(unsigned int)bitPos,
				(*errByte),
				*errByte ^ (1 << bitPos));
			*errByte ^= (1 << bitPos);
		}

		pErrPos++;
		ErrorNbr--;
	}
	return 0;
}

/**
 * \brief Launch error detection functions and correct corrupted bits.
 * \param pPmeccDescriptor Pointer to a PMECC_paramDesc instance.
 * \param pmeccStatus Value of the PMECC status register.
 * \param pageBuffer Base address of the buffer
 *	containing the page to be corrected.
 * \param ErrorNbr Number of error to correct
 * \return 0 if all errors have been corrected, 1 if too many errors detected
 */
unsigned int PMECC_CorrectionAlgo(unsigned long pPMECC,
		unsigned long pPMERRLOC,
		struct _PMECC_paramDesc_struct *pPmeccDescriptor,
		unsigned int pmeccStatus,
		void *pageBuffer)
{
	unsigned int sectorNumber = 0;
	unsigned int sectorBaseAddress, eccBaseAddr;
	volatile int errorNbr;
	unsigned int sector_num_per_page, page_size_byte, ecc_byte_per_sector;

	/* Set the sector size (512 or 1024 bytes) */
	pmecclor_writel((pPmeccDescriptor->sectorSize >> 4), PMERRLOC_ELCFG);

	sector_num_per_page = div(pPmeccDescriptor->eccSizeByte,
					get_pmecc_bytes());
	page_size_byte = sector_num_per_page * PMECC_SECTOR_SIZE;
	ecc_byte_per_sector = get_pmecc_bytes();

	while (sectorNumber < sector_num_per_page) {

		errorNbr = 0;
		if (pmeccStatus & 0x1) {

			sectorBaseAddress = (unsigned int)pageBuffer
					+ (sectorNumber * PMECC_SECTOR_SIZE);
			eccBaseAddr = (unsigned int)pageBuffer
					+ page_size_byte
					+ pmecc_readl(PMECC_SADDR)
					+ (sectorNumber * ecc_byte_per_sector);

			GenSyn(pPMECC, pPmeccDescriptor, sectorNumber);

			substitute(pPmeccDescriptor);

			get_sigma(pPmeccDescriptor);
			errorNbr = ErrorLocation(pPMERRLOC,
					pPmeccDescriptor,
					(((pPmeccDescriptor->sectorSize >> 4) + 1) * 512 * 8)
					+ (pPmeccDescriptor->tt
						* (13 + (pPmeccDescriptor->sectorSize >> 4))));

			if (errorNbr == -1)
				return 1;	/* uncorrectable errors */
			else
				ErrorCorrection(pPMERRLOC,
						pPmeccDescriptor,
						sectorBaseAddress,
						eccBaseAddr,
						ecc_byte_per_sector,
						errorNbr);
		}
		sectorNumber++;
		pmeccStatus = pmeccStatus >> 1;
	}

	return 0;
}

void buf_dump(unsigned char *buf, int offset, int len)
{
	int i = 0;
	for (i = 0; i < len; i++) {
		if (i % 16 == 0)
			dbg_loud("\n");
		dbg_loud("%u ", buf[offset + i]);
	}
}

void page_dump(unsigned char *buf, int page_size, int oob_size)
{
	dbg_loud("Dump Error Page: Data:\n");
	buf_dump(buf, 0, page_size);
	dbg_loud("\nOOB:\n");
	buf_dump(buf, page_size, oob_size);
	dbg_loud("\n");
}

int pmecc_process(struct nand_info *nand, unsigned char *buffer)
{
	int ret = 0;
	int result;
	unsigned int erris;

	/* waiting for PMECC ready */
	while (pmecc_readl(PMECC_SR) & AT91C_PMECC_BUSY)
		;

	/* read corrupted bit status */
	erris = pmecc_readl(PMECC_ISR);
	if (erris) {
		if (check_pmecc_ecc_data(nand, buffer) == -1)
			return 0;

		/* erris means which sector has errors. for example:
		 * if erris is 0x9 (0b1001)
		 *                    ^  ^
		 * the bit 1 indicate the position of error sectors.
		 * If we have 4 sectors, then that means the first
		 * and last sector has errors.
		 */
		dbg_info("PMECC: sector bits = %d, bit 1 means corrupted sector, Now correcting...\n", erris);
		result = PMECC_CorrectionAlgo(AT91C_BASE_PMECC,
					AT91C_BASE_PMERRLOC,
					&PMECC_paramDesc,
					erris,
					buffer);

		if (result != 0) {
			dbg_info("PMECC: failed to " \
					"correct corrupted bits!\n");
			ret =  -1;

			/* dump the whole page for test */
			page_dump(buffer, nand->pagesize, nand->oobsize);
		}
	}

	return ret;
}

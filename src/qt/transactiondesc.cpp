// Copyright (c) 2019 www.beancash.org

#include "transactiondesc.h"

#include "guiutil.h"
#include "bitbeanunits.h"
#include "transactionrecord.h"

#include "main.h"
#include "wallet.h"
#include "txdb.h"
#include "ui_interface.h"
#include "base58.h"

#include <string>

QString TransactionDesc::FormatTxStatus(const CWalletTx& wtx)
{
    if (!wtx.IsFinal())
    {
        if (wtx.nLockTime < LOCKTIME_THRESHOLD)
            return tr("Open for %n block(s)", "", wtx.nLockTime - nBestHeight + 1);
        else
            return tr("Open until %1").arg(GUIUtil::dateTimeStr(wtx.nLockTime));
    }
    else
    {
        int nDepth = wtx.GetDepthInMainChain();
        if (nDepth < 0)
            return tr("conflicted");
        else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
            return tr("%1/offline").arg(nDepth);
        else if (nDepth < 10)
            return tr("%1/unconfirmed").arg(nDepth);
        else
            return tr("%1 confirmations").arg(nDepth);
    }
}

QString TransactionDesc::toHTML(CWallet *wallet, CWalletTx &wtx, TransactionRecord *rec, int unit)
{
    QString strHTML;

    {
        LOCK(wallet->cs_wallet);
        strHTML.reserve(4000);
        strHTML += "<html><font face='verdana, arial, helvetica, sans-serif'>";

        int64_t nTime = wtx.GetTxTime();
        int64_t nCredit = wtx.GetCredit();
        int64_t nDebit = wtx.GetDebit();
        int64_t nNet = nCredit - nDebit;

        strHTML += "<b>" + tr("Status") + ":</b> " + FormatTxStatus(wtx);
        int nRequests = wtx.GetRequestCount();
        if (nRequests != -1)
        {
            if (nRequests == 0)
                strHTML += tr(", has not been successfully broadcast yet");
            else if (nRequests > 0)
                strHTML += tr(", broadcast through %n node(s)", "", nRequests);
        }
        strHTML += "<br>";

        strHTML += "<b>" + tr("Date") + ":</b> " + (nTime ? GUIUtil::dateTimeStr(nTime) : "") + "<br>";

        //
        // From
        //
        if (wtx.IsBeanBase() || wtx.IsBeanStake())
        {
            strHTML += "<b>" + tr("Source") + ":</b> " + tr("Generated") + "<br>";
        }
        else if (wtx.mapValue.count("from") && !wtx.mapValue["from"].empty())
        {
            // Online transaction
            strHTML += "<b>" + tr("From") + ":</b> " + GUIUtil::HtmlEscape(wtx.mapValue["from"]) + "<br>";
        }
        else
        {
            // Offline transaction
            if (nNet > 0)
            {
                // Credit
                if (CBitbeanAddress(rec->address).IsValid())
                {
                    CTxDestination address = CBitbeanAddress(rec->address).Get();
                    if (wallet->mapAddressBook.count(address))
                    {
                        strHTML += "<b>" + tr("From") + ":</b> " + tr("unknown") + "<br>";
                        strHTML += "<b>" + tr("To") + ":</b> ";
                        strHTML += GUIUtil::HtmlEscape(rec->address);
                        if (!wallet->mapAddressBook[address].name.empty())
                            strHTML += " (" + tr("own address") + ", " + tr("label") + ": " + GUIUtil::HtmlEscape(wallet->mapAddressBook[address].name) + ")";
                        else
                            strHTML += " (" + tr("own address") + ")";
                            strHTML += "<br>";
                    }
                }
            }
        }

        //
        // To
        //
        if (wtx.mapValue.count("to") && !wtx.mapValue["to"].empty())
        {
            // Online transaction
            std::string strAddress = wtx.mapValue["to"];
            strHTML += "<b>" + tr("To") + ":</b> ";
            CTxDestination dest = CBitbeanAddress(strAddress).Get();
            if (wallet->mapAddressBook.count(dest) && !wallet->mapAddressBook[dest].name.empty())
                strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[dest].name) + " ";
            strHTML += GUIUtil::HtmlEscape(strAddress) + "<br>";
        }

        //
        // Amount
        //
        if (wtx.IsBeanBase() && nCredit == 0)
        {
            //
            // Beanbase
            //
            int64_t nUnmatured = 0;
            for (const CTxOut& txout : wtx.vout)
                nUnmatured += wallet->GetCredit(txout);
            strHTML += "<b>" + tr("Credit") + ":</b> ";
            if (wtx.IsInMainChain())
                strHTML += BitbeanUnits::formatWithUnit(unit, nUnmatured)+ " (" + tr("matures in %n more block(s)", "", wtx.GetBlocksToMaturity()) + ")";
            else
                strHTML += "(" + tr("not accepted") + ")";
            strHTML += "<br>";
        }
        else if (nNet > 0)
        {
            //
            // Credit
            //
            strHTML += "<b>" + tr("Credit") + ":</b> " + BitbeanUnits::formatWithUnit(unit, nNet) + "<br>";
        }
        else
        {
            bool fAllFromMe = true;
            for (const CTxIn& txin : wtx.vin)
                fAllFromMe = fAllFromMe && wallet->IsMine(txin);

            bool fAllToMe = true;
            for (const CTxOut& txout : wtx.vout)
                fAllToMe = fAllToMe && wallet->IsMine(txout);

            if (fAllFromMe)
            {
                //
                // Debit
                //
                for (const CTxOut& txout : wtx.vout)
                {
                    if (wallet->IsMine(txout))
                        continue;

                    if (!wtx.mapValue.count("to") || wtx.mapValue["to"].empty())
                    {
                        // Offline transaction
                        CTxDestination address;
                        if (ExtractDestination(txout.scriptPubKey, address))
                        {
                            strHTML += "<b>" + tr("To") + ":</b> ";
                            if (wallet->mapAddressBook.count(address) && !wallet->mapAddressBook[address].name.empty())
                                strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[address].name) + " ";
                            strHTML += GUIUtil::HtmlEscape(CBitbeanAddress(address).ToString());
                            strHTML += "<br>";
                        }
                    }

                    strHTML += "<b>" + tr("Debit") + ":</b> " + BitbeanUnits::formatWithUnit(unit, -txout.nValue) + "<br>";
                }

                if (fAllToMe)
                {
                    // Payment to self
                    int64_t nChange = wtx.GetChange();
                    int64_t nValue = nCredit - nChange;
                    strHTML += "<b>" + tr("Debit") + ":</b> " + BitbeanUnits::formatWithUnit(unit, -nValue) + "<br>";
                    strHTML += "<b>" + tr("Credit") + ":</b> " + BitbeanUnits::formatWithUnit(unit, nValue) + "<br>";
                }

                int64_t nTxFee = nDebit - wtx.GetValueOut();
                if (nTxFee > 0)
                    strHTML += "<b>" + tr("Transaction fee") + ":</b> " + BitbeanUnits::formatWithUnit(unit, -nTxFee) + "<br>";
            }
            else
            {
                //
                // Mixed debit transaction
                //
                for (const CTxIn& txin : wtx.vin)
                    if (wallet->IsMine(txin))
                        strHTML += "<b>" + tr("Debit") + ":</b> " + BitbeanUnits::formatWithUnit(unit, -wallet->GetDebit(txin)) + "<br>";
                for (const CTxOut& txout : wtx.vout)
                    if (wallet->IsMine(txout))
                        strHTML += "<b>" + tr("Credit") + ":</b> " + BitbeanUnits::formatWithUnit(unit, wallet->GetCredit(txout)) + "<br>";
            }
        }

        strHTML += "<b>" + tr("Net amount") + ":</b> " + BitbeanUnits::formatWithUnit(unit, nNet, true) + "<br>";

        //
        // Message
        //
        if (wtx.mapValue.count("message") && !wtx.mapValue["message"].empty())
            strHTML += "<br><b>" + tr("Message") + ":</b><br>" + GUIUtil::HtmlEscape(wtx.mapValue["message"], true) + "<br>";
        if (wtx.mapValue.count("comment") && !wtx.mapValue["comment"].empty())
            strHTML += "<br><b>" + tr("Comment") + ":</b><br>" + GUIUtil::HtmlEscape(wtx.mapValue["comment"], true) + "<br>";

        strHTML += "<b>" + tr("Transaction ID") + ":</b> " + rec->getTxID() + "<br>";
        strHTML += "<b>" + tr("Output index") + ":</b> " + QString::number(rec->getOutputIndex()) + "<br>";

        if (wtx.IsBeanBase() || wtx.IsBeanStake())
            strHTML += "<br>" + tr("Generated beans must mature 110 blocks before they can be spent. When you generated this block, it was broadcast to the network to be added to the block chain. If it fails to get into the chain, its state will change to \"not accepted\" and it won't be spendable. This may occasionally happen if another node generates a block within a few seconds of yours.") + "<br>";

        //
        // Debug view
        //
        if (fDebug)
        {
            strHTML += "<hr><br>" + tr("Debug information") + "<br><br>";
            for (const CTxIn& txin : wtx.vin)
                if(wallet->IsMine(txin))
                    strHTML += "<b>" + tr("Debit") + ":</b> " + BitbeanUnits::formatWithUnit(unit, -wallet->GetDebit(txin)) + "<br>";
            for (const CTxOut& txout : wtx.vout)
                if(wallet->IsMine(txout))
                    strHTML += "<b>" + tr("Credit") + ":</b> " + BitbeanUnits::formatWithUnit(unit, wallet->GetCredit(txout)) + "<br>";

            strHTML += "<br><b>" + tr("Transaction") + ":</b><br>";
            strHTML += GUIUtil::HtmlEscape(wtx.ToString(), true);

            CTxDB txdb("r"); // To fetch source txouts

            strHTML += "<br><b>" + tr("Inputs") + ":</b>";
            strHTML += "<ul>";

            {
                LOCK(wallet->cs_wallet);
                for (const CTxIn& txin : wtx.vin)
                {
                    COutPoint prevout = txin.prevout;

                    CTransaction prev;
                    if(txdb.ReadDiskTx(prevout.hash, prev))
                    {
                        if (prevout.n < prev.vout.size())
                        {
                            strHTML += "<li>";
                            const CTxOut &vout = prev.vout[prevout.n];
                            CTxDestination address;
                            if (ExtractDestination(vout.scriptPubKey, address))
                            {
                                if (wallet->mapAddressBook.count(address) && !wallet->mapAddressBook[address].name.empty())
                                    strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[address].name) + " ";
                                strHTML += QString::fromStdString(CBitbeanAddress(address).ToString());
                            }
                            strHTML = strHTML + " " + tr("Amount") + "=" + BitbeanUnits::formatWithUnit(unit, vout.nValue);
                            strHTML = strHTML + " IsMine=" + (wallet->IsMine(vout) ? tr("true") : tr("false")) + "</li>";
                        }
                    }
                }
            }

            strHTML += "</ul>";
        }

        strHTML += "</font></html>";
    }
    return strHTML;
}

#!/usr/bin/env python3
"""Generate the "bigunion" benchmark family: bind-time union_by_name inference.

Usage: python3 gen.py --out <data_root> --split dev|holdout [--files N]

Writes into <data_root>/bigunion/<split>/. Output is byte-identical across
runs because every generator uses a fixed seed and never reads the clock. The
split directory is removed and rebuilt on every run.

Each file is one access-profile document of 2-8 MB (all under the 16 MiB
maximum_file_size, so inference parses every file with DOM). Files drift in
which optional sections and optional children they carry, and a few scalar
fields drift between INTEGER and VARCHAR, so union_by_name has to read every
file to reach the full schema.

dev layout:
  profiles/profile-NNN.xml     240 files, 2-8 MB, <Profile> root, one record per file

holdout layout (different seed):
  profiles/profile-NNN.xml     120 files, 2-8 MB
"""

import argparse
import os
import random
import shutil

FAMILY = "bigunion"

OBJECTS = ["Account", "Contact", "Lead", "Opportunity", "Case", "Campaign", "Contract", "Order",
           "Product2", "Pricebook2", "Quote", "Asset", "Entitlement", "WorkOrder", "Invoice__c",
           "Shipment__c", "Region__c", "Territory__c", "Survey__c", "Ticket__c"]
FIELDS = ["Name", "Owner", "Status", "Amount", "Stage", "Region", "Phone", "Email", "Website",
          "Industry", "Rating", "Source", "Priority", "Origin", "Subject", "Description", "Type",
          "Score__c", "Tier__c", "Segment__c", "Notes__c", "External_Id__c", "Due__c"]
PERMS = ["ApiEnabled", "ViewSetup", "ModifyAllData", "ViewAllData", "ManageUsers", "RunReports",
         "ExportReport", "EditTask", "EditEvent", "ManageDashboards", "ImportLeads", "TransferAnyLead",
         "ConvertLeads", "ManageCases", "EditOppLineItemUnitPrice", "ActivitiesAccess"]
APPS = ["standard__Sales", "standard__Service", "standard__Marketing", "Ops_Console", "Field_App"]
CLASSES = ["AccountService", "LeadRouter", "CaseTriggerHandler", "QuoteBuilder", "InvoiceSync",
           "TerritoryRules", "ShipmentTracker", "SurveyMailer"]
VIS = ["DefaultOn", "DefaultOff", "Hidden"]


def b(rng):
    return "true" if rng.random() < 0.5 else "false"


def field_permission(rng, obj, field, with_hidden):
    out = ["    <fieldPermissions>",
           f"        <editable>{b(rng)}</editable>",
           f"        <field>{obj}.{field}</field>",
           f"        <readable>{b(rng)}</readable>"]
    if with_hidden:
        out.append(f"        <hidden>{b(rng)}</hidden>")
    out.append("    </fieldPermissions>")
    return out


def gen_profile(rng, idx, target_bytes):
    # Per-file drift: which optional sections and children this file carries.
    rev = rng.randint(1, 5)
    with_hidden = rev >= 3
    with_tabs = rng.random() < 0.5
    with_classes = rng.random() < 0.7
    with_record_types = rng.random() < 0.4
    with_ip_ranges = rng.random() < 0.08
    with_flow_access = rev >= 4
    session_textual = rng.random() < 0.15

    out = ['<?xml version="1.0" encoding="UTF-8"?>', "<Profile>"]
    for app in rng.sample(APPS, rng.randint(2, len(APPS))):
        out.append("    <applicationVisibilities>")
        out.append(f"        <application>{app}</application>")
        out.append(f"        <default>{b(rng)}</default>")
        out.append(f"        <visible>{b(rng)}</visible>")
        out.append("    </applicationVisibilities>")
    if with_classes:
        for cls in rng.sample(CLASSES, rng.randint(1, len(CLASSES))):
            out.append(f"    <classAccesses><apexClass>{cls}</apexClass><enabled>{b(rng)}</enabled></classAccesses>")
    out.append(f"    <custom>{b(rng)}</custom>")
    if rng.random() < 0.6:
        out.append(f"    <description>Profile revision {rev} for team {idx}</description>")

    # The bulk of the bytes: fieldPermissions over synthetic object/field names.
    size = sum(len(line) + 1 for line in out)
    n = 0
    while size < target_bytes:
        obj = OBJECTS[n % len(OBJECTS)] if n < len(OBJECTS) else f"{rng.choice(OBJECTS)}_{n // 50}"
        for line in field_permission(rng, obj, rng.choice(FIELDS) + f"_{n}", with_hidden):
            out.append(line)
            size += len(line) + 1
        n += 1

    if with_flow_access:
        for k in range(rng.randint(1, 6)):
            out.append(f"    <flowAccesses><enabled>{b(rng)}</enabled><flow>Flow_{k}</flow></flowAccesses>")
    for obj in OBJECTS:
        out.append("    <layoutAssignments>")
        out.append(f"        <layout>{obj}-Layout</layout>")
        if with_record_types and rng.random() < 0.5:
            out.append(f"        <recordType>{obj}.Business</recordType>")
        out.append("    </layoutAssignments>")
    if with_ip_ranges:
        for k in range(rng.randint(1, 3)):
            out.append("    <loginIpRanges>")
            if rng.random() < 0.5:
                out.append(f"        <description>Office {k}</description>")
            out.append(f"        <endAddress>10.{k}.255.255</endAddress>")
            out.append(f"        <startAddress>10.{k}.0.0</startAddress>")
            out.append("    </loginIpRanges>")
    for obj in OBJECTS:
        out.append("    <objectPermissions>")
        for p in ("allowCreate", "allowDelete", "allowEdit", "allowRead", "modifyAllRecords"):
            out.append(f"        <{p}>{b(rng)}</{p}>")
        out.append(f"        <object>{obj}</object>")
        out.append(f"        <viewAllRecords>{b(rng)}</viewAllRecords>")
        out.append("    </objectPermissions>")
    if with_record_types:
        for obj in rng.sample(OBJECTS, 6):
            out.append("    <recordTypeVisibilities>")
            out.append(f"        <default>{b(rng)}</default>")
            out.append(f"        <recordType>{obj}.Business</recordType>")
            out.append(f"        <visible>{b(rng)}</visible>")
            out.append("    </recordTypeVisibilities>")
    timeout = "TwoHours" if session_textual else str(rng.choice([15, 30, 60, 120]))
    out.append(f"    <sessionTimeout>{timeout}</sessionTimeout>")
    if with_tabs:
        for obj in OBJECTS:
            out.append(f"    <tabVisibilities><tab>standard-{obj}</tab><visibility>{rng.choice(VIS)}</visibility></tabVisibilities>")
    out.append("    <userLicense>Salesforce</userLicense>")
    for p in rng.sample(PERMS, rng.randint(6, len(PERMS))):
        out.append(f"    <userPermissions><enabled>{b(rng)}</enabled><name>{p}</name></userPermissions>")
    out.append("</Profile>")
    return "\n".join(out) + "\n"


def gen_profiles(root, rng, nfiles):
    d = os.path.join(root, "profiles")
    os.makedirs(d)
    for i in range(nfiles):
        target = rng.randint(2, 8) * 1024 * 1024
        with open(os.path.join(d, f"profile-{i:03d}.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write(gen_profile(rng, i, target))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--split", required=True, choices=["dev", "holdout"])
    ap.add_argument("--files", type=int, help="override the file count (for quick local runs)")
    args = ap.parse_args()

    root = os.path.join(args.out, FAMILY, args.split)
    if os.path.isdir(root):
        shutil.rmtree(root)
    os.makedirs(root)

    if args.split == "dev":
        gen_profiles(root, random.Random(3101), args.files or 240)
    else:
        gen_profiles(root, random.Random(3201), args.files or 120)


if __name__ == "__main__":
    main()

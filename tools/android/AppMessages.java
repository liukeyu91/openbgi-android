/* SPDX-License-Identifier: GPL-2.0-only */
package org.openbgi.port;

import android.content.Context;
import java.io.File;
import java.util.Locale;

/** Formats game metadata for the UI's current Android resource configuration. */
final class AppMessages {
    private AppMessages() {}

    static String encodingName(String encoding) {
        return encoding.equals("utf8") ? "UTF-8" : encoding.equals("big5") ? "Big5" : encoding.toUpperCase(Locale.ROOT);
    }

    static String encodingLabel(Context context, GameSettings settings) {
        if (!settings.automaticEncoding) return encodingName(settings.encoding);
        if (settings.encodingReady()) return context.getString(R.string.encoding_auto_label, encodingName(settings.encoding));
        return context.getString(settings.needsEncodingDetection() ? R.string.encoding_pending_label : R.string.encoding_unknown_label);
    }

    static String encodingStatus(Context context, GameSettings settings) {
        if (!settings.automaticEncoding) return context.getString(R.string.encoding_manual, encodingName(settings.encoding));
        if (settings.encodingReady()) return context.getString(R.string.encoding_detected, encodingName(settings.encoding));
        switch (settings.encodingDetection) {
            case "mixed": return context.getString(R.string.encoding_mixed);
            case "error": return context.getString(R.string.encoding_sample_error);
            default: return context.getString(settings.needsEncodingDetection() ? R.string.encoding_detect_pending : R.string.encoding_unclear);
        }
    }

    static String encodingAdvice(Context context, EncodingDetector.Result result) {
        if (result.usable == 0) return context.getString(R.string.encoding_advice_no_sample);
        String advice = result.recommended == null ? context.getString(R.string.encoding_advice_unclear)
            : context.getString(R.string.encoding_advice_suggested, encodingName(result.recommended));
        return result.mixed ? advice + "\n" + context.getString(R.string.encoding_advice_mixed) : advice;
    }

    static String encodingPreview(Context context, byte[] bytes, int encoding) {
        String text = EncodingDetector.decode(bytes, encoding);
        return text == null ? context.getString(R.string.encoding_invalid_bytes) : text;
    }

    static String repairError(Context context, InitialTextRepair.Error error) {
        switch (error) {
            case DECODE: return context.getString(R.string.repair_decode_error);
            case UNREPRESENTABLE: return context.getString(R.string.repair_unrepresentable);
            case TOO_LONG: return context.getString(R.string.repair_too_long);
            case DISALLOWED: return context.getString(R.string.repair_disallowed);
            default: return context.getString(R.string.repair_unknown);
        }
    }

    static String patchSummary(Context context, PatchImport.Result result) {
        if (result.repaired > 0) return context.getString(R.string.patch_repaired, result.resources, result.repaired);
        if (result.resources > 0) return context.getString(R.string.patch_extracted, result.resources);
        if (result.preserved > 0) return context.getString(R.string.patch_ready);
        if (!result.profile.isEmpty()) return context.getString(R.string.patch_identified);
        return context.getString(result.containers > 0 ? R.string.patch_empty_container : R.string.patch_not_needed);
    }

    static String patchCheck(Context context,PatchDoctor.Result result){
        int status=result.status==PatchDoctor.Status.REJECT?R.string.patch_check_reject:
            result.status==PatchDoctor.Status.WARNING?R.string.patch_check_warning:R.string.patch_check_pass;
        StringBuilder text=new StringBuilder(context.getString(status))
            .append("\n").append(context.getString(R.string.patch_check_counts,result.archives,result.samples));
        for(PatchDoctor.Finding finding:result.findings){
            int message;
            switch(finding.code){
                case INVALID_MANIFEST:message=R.string.patch_check_manifest;break;
                case UNKNOWN_TARGET:message=R.string.patch_check_unknown;break;
                case WRONG_PRODUCT:message=R.string.patch_check_product;break;
                case WRONG_ENGINE:message=R.string.patch_check_engine;break;
                case WRONG_BOOT:message=R.string.patch_check_boot;break;
                case BAD_ARCHIVE:message=R.string.patch_check_archive;break;
                case WRONG_ENCODING:message=R.string.patch_check_encoding;break;
                case UNDECODABLE_TEXT:message=R.string.patch_check_undecodable;break;
                case MIXED_ENCODING:message=R.string.patch_check_mixed;break;
                case NO_TEXT:message=R.string.patch_check_no_text;break;
                case BAD_GLYPHS:message=R.string.patch_check_glyphs;break;
                case NO_FONT:message=R.string.patch_check_no_font;break;
                case FONT_SETTING:message=R.string.patch_check_font;break;
                case TEXT_WIDTH:message=R.string.patch_check_width;break;
                case NAME_CONFLICT:message=R.string.patch_check_conflict;break;
                default:message=R.string.patch_check_import;break;
            }
            text.append("\n\n").append(context.getString(message,finding.arguments));
        }
        return text.toString();
    }

    private static String compatibilitySummary(Context context, ProgramCompatibility.Status status, int findings) {
        switch (status) {
            case RECHECK: return context.getString(R.string.compat_recheck);
            case INCOMPLETE: return context.getString(R.string.compat_not_complete);
            case FINDINGS: return context.getString(R.string.compat_findings, findings);
            case PARTIAL: return context.getString(R.string.compat_partial);
            case PASS: return context.getString(R.string.compat_pass);
            default: return context.getString(R.string.compat_unchecked);
        }
    }

    static String cachedCompatibility(Context context, File game, File resources, String preference, String version) {
        ProgramCompatibility.Summary summary = ProgramCompatibility.cachedSummary(game, resources, preference, version);
        return compatibilitySummary(context, summary.status, summary.findingCount);
    }

    static String compatibilityReport(Context context, ProgramCompatibility.Result result) {
        String profile = result.profile.isEmpty() ? context.getString(R.string.compat_profile_unknown) : result.profile;
        StringBuilder text = new StringBuilder(compatibilitySummary(context, result.status(), result.findings.size()))
            .append("\n\n").append(context.getString(R.string.compat_profile, profile))
            .append('\n').append(context.getString(R.string.compat_counts, result.checked, result.programs, result.instructions));
        if (!result.error.isEmpty()) text.append('\n').append(result.error);
        if (result.profile.isEmpty()) text.append('\n').append(context.getString(R.string.compat_choose_profile));
        int shown = 0;
        for (ProgramCompatibility.Finding finding : result.findings.values()) {
            if (shown++ == ProgramCompatibility.MAX_DETAILS) {
                text.append("\n\n").append(context.getString(R.string.compat_more));
                break;
            }
            String opcode = String.format(Locale.ROOT, finding.opcode > 255 ? "$%04X" : "$%02X", finding.opcode);
            int reason = finding.kind == 1 ? R.string.compat_not_implemented
                : finding.kind == 3 ? R.string.compat_external : R.string.compat_unsupported;
            text.append("\n\n").append(context.getString(R.string.compat_finding, opcode, context.getString(reason)));
            for (String location : finding.locations) text.append('\n').append(location);
            if (finding.occurrences > finding.locations.size()) {
                text.append('\n').append(context.getString(R.string.compat_more_locations, finding.occurrences - finding.locations.size()));
            }
        }
        if (result.skipped > 0) text.append("\n\n").append(context.getString(R.string.compat_skipped, result.skipped));
        if (result.problems > 0) {
            text.append('\n').append(context.getString(R.string.compat_problems, result.problems));
            for (String location : result.problemLocations) text.append('\n').append(location);
        }
        if (result.limited) text.append('\n').append(context.getString(R.string.compat_limited));
        return text.append("\n\n").append(context.getString(R.string.compat_scope)).toString();
    }
}

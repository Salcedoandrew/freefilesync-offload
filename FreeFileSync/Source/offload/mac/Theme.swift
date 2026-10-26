// *****************************************************************************
// * Offload window - colours and metrics.                                     *
// *                                                                           *
// * One place for the palette so the window reads as one surface: near-black   *
// * greys for structure, saturated green for verified, blue for moving bytes.   *
// *****************************************************************************

import SwiftUI

extension Color
{
    init(hex: UInt32, alpha: Double = 1)
    {
        self.init(.sRGB,
                  red:   Double((hex >> 16) & 0xFF) / 255,
                  green: Double((hex >>  8) & 0xFF) / 255,
                  blue:  Double( hex        & 0xFF) / 255,
                  opacity: alpha)
    }
}

enum Theme
{
    static let windowBG     = Color(hex: 0x121315)
    static let paneBG       = Color(hex: 0x181A1C)
    static let columnBG     = Color(hex: 0x1B1D20)
    static let stroke       = Color(hex: 0x2A2C30)
    static let strokeSoft   = Color(hex: 0x232528)

    static let textPrimary   = Color(hex: 0xE9EBEE)
    static let textSecondary = Color(hex: 0x8C9199)
    static let textTertiary  = Color(hex: 0x676C74)

    static let track    = Color(hex: 0x282A2E)
    static let green    = Color(hex: 0x3ED94D)
    static let greenDim = Color(hex: 0x2FA83B)
    static let blue     = Color(hex: 0x1B7BF5)
    static let blueDim  = Color(hex: 0x0A57C4)
    static let amber    = Color(hex: 0xFFB020)
    static let red      = Color(hex: 0xFF453A)

    static let buttonBG   = Color(hex: 0x2C2F33)
    static let buttonBG2  = Color(hex: 0x232528)
    static let accent     = Color(hex: 0x0A6CFF)

    static let paneHeader: Font = .system(size: 11, weight: .regular)
    static let rowTitle:   Font = .system(size: 12, weight: .regular)
    static let rowBold:    Font = .system(size: 12, weight: .semibold)
    static let rowSmall:   Font = .system(size: 10, weight: .regular)
    static let deviceName: Font = .system(size: 11, weight: .medium)
    static let deviceSub:  Font = .system(size: 9.5, weight: .regular)
}

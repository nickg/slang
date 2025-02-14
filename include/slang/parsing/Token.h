//------------------------------------------------------------------------------
//! @file Token.h
//! @brief Contains the Token class and related helpers
//
// File is under the MIT license; see LICENSE for details
//------------------------------------------------------------------------------
#pragma once

#include "slang/numeric/SVInt.h"
#include "slang/numeric/Time.h"
#include "slang/text/SourceLocation.h"
#include "slang/util/SmallVector.h"
#include "slang/util/Util.h"

namespace slang {

enum class SyntaxKind;
enum class TokenKind : uint16_t;

class Diagnostics;
class SyntaxNode;
class Token;

/// Various flags for numeric tokens.
struct NumericTokenFlags {
    uint8_t raw = 0;

    LiteralBase base() const { return LiteralBase(raw & 0b11); }
    bool isSigned() const { return (raw & 0b100) != 0; }
    TimeUnit unit() const { return TimeUnit((raw & 0b111000) >> 3); }
    bool outOfRange() const { return (raw & 0b1000000) != 0; }

    void set(LiteralBase base, bool isSigned);
    void set(TimeUnit unit);
    void setOutOfRange(bool value);
};

/// The kind of trivia we've stored.
enum class TriviaKind : uint8_t {
    Unknown,
    Whitespace,
    EndOfLine,
    LineComment,
    BlockComment,
    DisabledText,
    SkippedTokens,
    SkippedSyntax,
    Directive
};

/// The Trivia class holds on to a piece of source text that should otherwise
/// not turn into a token; for example, a preprocessor directive, a line continuation
/// character, or a comment.
class Trivia {
private:
#pragma pack(push, 4)
    struct ShortStringView {
        const char* ptr;
        uint32_t len;
    };
    struct ShortTokenSpan {
        const Token* ptr;
        uint32_t len;
    };
    struct FullLocation {
        string_view text;
        SourceLocation location;
    };

    union {
        ShortStringView rawText;
        ShortTokenSpan tokens;
        SyntaxNode* syntaxNode;
        FullLocation* fullLocation;
    };
#pragma pack(pop)

    // The vast majority of trivia lives alongside the token it's attached to, so if
    // you want to know its source location just walk backward from the parent location.
    // For certain cases though (the preprocessor inserted tokens) the trivia gets glued
    // together from different locations. In that case hasFullLocation will be true and
    // the union will point at a FullLocation structure.
    bool hasFullLocation = false;

public:
    TriviaKind kind;

    Trivia();
    Trivia(TriviaKind kind, string_view rawText);
    Trivia(TriviaKind kind, span<Token const> tokens);
    Trivia(TriviaKind kind, SyntaxNode* syntax);

    /// If the trivia is raw source text, creates a new trivia with the specified location
    /// (instead of implicitly offset from the parent token). If this trivia is for a
    /// directive or skipped tokens, returns a copy without modification.
    [[nodiscard]] Trivia withLocation(BumpAllocator& alloc, SourceLocation location) const;

    /// Gets the source location of the trivia if one is explicitly known. If not, nullopt
    /// is returned to signify that the location is implicitly relative to the parent token.
    optional<SourceLocation> getExplicitLocation() const;

    /// If this trivia is tracking a skipped syntax node or a directive, returns that node.
    /// Otherwise returns nullptr.
    SyntaxNode* syntax() const;

    /// Get the raw text of the trivia, if any.
    string_view getRawText() const;

    /// If the trivia represents skipped tokens, returns the list of tokens that were
    /// skipped. Otherwise returns an empty span.
    span<Token const> getSkippedTokens() const;
};
static_assert(sizeof(Trivia) == 16);

/// Represents a single lexed token, including leading trivia, original location, token kind,
/// and any related information derived from the token itself (such as the lexeme).
///
/// This class is a lightweight immutable structure designed to be copied around and stored
/// wherever. The bulk of the token's data is stored in a heap allocated block. Most of the
/// hot path only cares about the token's kind, so that's given priority.
class Token {
public:
    /// The kind of the token; this is not in the info block because
    /// we almost always want to look at it (perf).
    TokenKind kind;

    Token();
    Token(BumpAllocator& alloc, TokenKind kind, span<Trivia const> trivia, string_view rawText,
          SourceLocation location);
    Token(BumpAllocator& alloc, TokenKind kind, span<Trivia const> trivia, string_view rawText,
          SourceLocation location, string_view strText);
    Token(BumpAllocator& alloc, TokenKind kind, span<Trivia const> trivia, string_view rawText,
          SourceLocation location, SyntaxKind directive);
    Token(BumpAllocator& alloc, TokenKind kind, span<Trivia const> trivia, string_view rawText,
          SourceLocation location, logic_t bit);
    Token(BumpAllocator& alloc, TokenKind kind, span<Trivia const> trivia, string_view rawText,
          SourceLocation location, const SVInt& value);
    Token(BumpAllocator& alloc, TokenKind kind, span<Trivia const> trivia, string_view rawText,
          SourceLocation location, double value, bool outOfRange, optional<TimeUnit> timeUnit);
    Token(BumpAllocator& alloc, TokenKind kind, span<Trivia const> trivia, string_view rawText,
          SourceLocation location, LiteralBase base, bool isSigned);

    /// A missing token was expected and inserted by the parser at a given point.
    bool isMissing() const { return missing; }

    SourceRange range() const;
    SourceLocation location() const;
    span<Trivia const> trivia() const;

    /// Value text is the "nice" lexed version of certain tokens;
    /// for example, in string literals, escape sequences are converted appropriately.
    string_view valueText() const;

    /// Gets the original lexeme that led to the creation of this token.
    string_view rawText() const;

    /// Prints the token (including all of its trivia) to a string.
    std::string toString() const;

    /// Data accessors for specific kinds of tokens.
    /// These will generally assert if the kind is wrong.
    SVInt intValue() const;
    double realValue() const;
    logic_t bitValue() const;
    NumericTokenFlags numericFlags() const;
    SyntaxKind directiveKind() const;

    /// Returns true if this token is on the same line as the token before it.
    /// This is detected by examining the leading trivia of this token for newlines.
    bool isOnSameLine() const;

    bool valid() const { return info != nullptr; }
    explicit operator bool() const { return valid(); }

    bool operator==(const Token& other) const { return kind == other.kind && info == other.info; }
    bool operator!=(const Token& other) const { return !(*this == other); }

    /// Modification methods to make it easier to deal with immutable tokens.
    [[nodiscard]] Token withTrivia(BumpAllocator& alloc, span<Trivia const> trivia) const;
    [[nodiscard]] Token withLocation(BumpAllocator& alloc, SourceLocation location) const;
    [[nodiscard]] Token withRawText(BumpAllocator& alloc, string_view rawText) const;
    [[nodiscard]] Token clone(BumpAllocator& alloc, span<Trivia const> trivia, string_view rawText,
                              SourceLocation location) const;

    static Token createMissing(BumpAllocator& alloc, TokenKind kind, SourceLocation location);
    static Token createExpected(BumpAllocator& alloc, Diagnostics& diagnostics, Token actual,
                                TokenKind expected, Token lastConsumed, Token matchingDelim);

private:
    struct Info;

    void init(BumpAllocator& alloc, TokenKind kind, span<Trivia const> trivia, string_view rawText,
              SourceLocation location);

    // Some data is stored directly in the token here because we have 6 bytes of padding that
    // would otherwise go unused. The rest is stored in the info block.
    bool missing : 1;
    uint8_t triviaCountSmall : 4;
    uint8_t reserved : 3;
    NumericTokenFlags numFlags;
    uint32_t rawLen = 0;
    Info* info = nullptr;

    // We use some free bits in the token structure to count how many trivia elements
    // this token has. This is enough space for the vast majority of tokens, but for
    // cases with more, triviaCountSmall gets set to all 1's and the real count is
    // included in the info structure.
    static constexpr int MaxTriviaSmallCount = (1 << 4) - 2;
};

static_assert(sizeof(Token) == 16);
static_assert(std::is_trivially_copyable_v<Token>);

enum class TokenKind : uint16_t {
    // general
    Unknown,
    EndOfFile,
    Identifier,
    SystemIdentifier,
    StringLiteral,
    IntegerLiteral,
    IntegerBase,
    UnbasedUnsizedLiteral,
    RealLiteral,
    TimeLiteral,
    Placeholder,

    // punctuation
    Apostrophe,
    ApostropheOpenBrace,
    OpenBrace,
    CloseBrace,
    OpenBracket,
    CloseBracket,
    OpenParenthesis,
    OpenParenthesisStar,
    CloseParenthesis,
    StarCloseParenthesis,
    Semicolon,
    Colon,
    ColonEquals,
    ColonSlash,
    DoubleColon,
    Comma,
    DotStar,
    Dot,
    Slash,
    Star,
    DoubleStar,
    StarArrow,
    Plus,
    DoublePlus,
    PlusColon,
    Minus,
    DoubleMinus,
    MinusColon,
    MinusArrow,
    MinusDoubleArrow,
    Tilde,
    TildeAnd,
    TildeOr,
    TildeXor,
    Dollar,
    Question,
    Hash,
    DoubleHash,
    HashMinusHash,
    HashEqualsHash,
    Xor,
    XorTilde,
    Equals,
    DoubleEquals,
    DoubleEqualsQuestion,
    TripleEquals,
    EqualsArrow,
    PlusEqual,
    MinusEqual,
    SlashEqual,
    StarEqual,
    AndEqual,
    OrEqual,
    PercentEqual,
    XorEqual,
    LeftShiftEqual,
    TripleLeftShiftEqual,
    RightShiftEqual,
    TripleRightShiftEqual,
    LeftShift,
    RightShift,
    TripleLeftShift,
    TripleRightShift,
    Exclamation,
    ExclamationEquals,
    ExclamationEqualsQuestion,
    ExclamationDoubleEquals,
    Percent,
    LessThan,
    LessThanEquals,
    LessThanMinusArrow,
    GreaterThan,
    GreaterThanEquals,
    Or,
    DoubleOr,
    OrMinusArrow,
    OrEqualsArrow,
    At,
    DoubleAt,
    And,
    DoubleAnd,
    TripleAnd,

    // keywords
    OneStep,
    AcceptOnKeyword,
    AliasKeyword,
    AlwaysKeyword,
    AlwaysCombKeyword,
    AlwaysFFKeyword,
    AlwaysLatchKeyword,
    AndKeyword,
    AssertKeyword,
    AssignKeyword,
    AssumeKeyword,
    AutomaticKeyword,
    BeforeKeyword,
    BeginKeyword,
    BindKeyword,
    BinsKeyword,
    BinsOfKeyword,
    BitKeyword,
    BreakKeyword,
    BufKeyword,
    BufIf0Keyword,
    BufIf1Keyword,
    ByteKeyword,
    CaseKeyword,
    CaseXKeyword,
    CaseZKeyword,
    CellKeyword,
    CHandleKeyword,
    CheckerKeyword,
    ClassKeyword,
    ClockingKeyword,
    CmosKeyword,
    ConfigKeyword,
    ConstKeyword,
    ConstraintKeyword,
    ContextKeyword,
    ContinueKeyword,
    CoverKeyword,
    CoverGroupKeyword,
    CoverPointKeyword,
    CrossKeyword,
    DeassignKeyword,
    DefaultKeyword,
    DefParamKeyword,
    DesignKeyword,
    DisableKeyword,
    DistKeyword,
    DoKeyword,
    EdgeKeyword,
    ElseKeyword,
    EndKeyword,
    EndCaseKeyword,
    EndCheckerKeyword,
    EndClassKeyword,
    EndClockingKeyword,
    EndConfigKeyword,
    EndFunctionKeyword,
    EndGenerateKeyword,
    EndGroupKeyword,
    EndInterfaceKeyword,
    EndModuleKeyword,
    EndPackageKeyword,
    EndPrimitiveKeyword,
    EndProgramKeyword,
    EndPropertyKeyword,
    EndSpecifyKeyword,
    EndSequenceKeyword,
    EndTableKeyword,
    EndTaskKeyword,
    EnumKeyword,
    EventKeyword,
    EventuallyKeyword,
    ExpectKeyword,
    ExportKeyword,
    ExtendsKeyword,
    ExternKeyword,
    FinalKeyword,
    FirstMatchKeyword,
    ForKeyword,
    ForceKeyword,
    ForeachKeyword,
    ForeverKeyword,
    ForkKeyword,
    ForkJoinKeyword,
    FunctionKeyword,
    GenerateKeyword,
    GenVarKeyword,
    GlobalKeyword,
    HighZ0Keyword,
    HighZ1Keyword,
    IfKeyword,
    IffKeyword,
    IfNoneKeyword,
    IgnoreBinsKeyword,
    IllegalBinsKeyword,
    ImplementsKeyword,
    ImpliesKeyword,
    ImportKeyword,
    IncDirKeyword,
    IncludeKeyword,
    InitialKeyword,
    InOutKeyword,
    InputKeyword,
    InsideKeyword,
    InstanceKeyword,
    IntKeyword,
    IntegerKeyword,
    InterconnectKeyword,
    InterfaceKeyword,
    IntersectKeyword,
    JoinKeyword,
    JoinAnyKeyword,
    JoinNoneKeyword,
    LargeKeyword,
    LetKeyword,
    LibListKeyword,
    LibraryKeyword,
    LocalKeyword,
    LocalParamKeyword,
    LogicKeyword,
    LongIntKeyword,
    MacromoduleKeyword,
    MatchesKeyword,
    MediumKeyword,
    ModPortKeyword,
    ModuleKeyword,
    NandKeyword,
    NegEdgeKeyword,
    NetTypeKeyword,
    NewKeyword,
    NextTimeKeyword,
    NmosKeyword,
    NorKeyword,
    NoShowCancelledKeyword,
    NotKeyword,
    NotIf0Keyword,
    NotIf1Keyword,
    NullKeyword,
    OrKeyword,
    OutputKeyword,
    PackageKeyword,
    PackedKeyword,
    ParameterKeyword,
    PmosKeyword,
    PosEdgeKeyword,
    PrimitiveKeyword,
    PriorityKeyword,
    ProgramKeyword,
    PropertyKeyword,
    ProtectedKeyword,
    Pull0Keyword,
    Pull1Keyword,
    PullDownKeyword,
    PullUpKeyword,
    PulseStyleOnDetectKeyword,
    PulseStyleOnEventKeyword,
    PureKeyword,
    RandKeyword,
    RandCKeyword,
    RandCaseKeyword,
    RandSequenceKeyword,
    RcmosKeyword,
    RealKeyword,
    RealTimeKeyword,
    RefKeyword,
    RegKeyword,
    RejectOnKeyword,
    ReleaseKeyword,
    RepeatKeyword,
    RestrictKeyword,
    ReturnKeyword,
    RnmosKeyword,
    RpmosKeyword,
    RtranKeyword,
    RtranIf0Keyword,
    RtranIf1Keyword,
    SAlwaysKeyword,
    SEventuallyKeyword,
    SNextTimeKeyword,
    SUntilKeyword,
    SUntilWithKeyword,
    ScalaredKeyword,
    SequenceKeyword,
    ShortIntKeyword,
    ShortRealKeyword,
    ShowCancelledKeyword,
    SignedKeyword,
    SmallKeyword,
    SoftKeyword,
    SolveKeyword,
    SpecifyKeyword,
    SpecParamKeyword,
    StaticKeyword,
    StringKeyword,
    StrongKeyword,
    Strong0Keyword,
    Strong1Keyword,
    StructKeyword,
    SuperKeyword,
    Supply0Keyword,
    Supply1Keyword,
    SyncAcceptOnKeyword,
    SyncRejectOnKeyword,
    TableKeyword,
    TaggedKeyword,
    TaskKeyword,
    ThisKeyword,
    ThroughoutKeyword,
    TimeKeyword,
    TimePrecisionKeyword,
    TimeUnitKeyword,
    TranKeyword,
    TranIf0Keyword,
    TranIf1Keyword,
    TriKeyword,
    Tri0Keyword,
    Tri1Keyword,
    TriAndKeyword,
    TriOrKeyword,
    TriRegKeyword,
    TypeKeyword,
    TypedefKeyword,
    UnionKeyword,
    UniqueKeyword,
    Unique0Keyword,
    UnsignedKeyword,
    UntilKeyword,
    UntilWithKeyword,
    UntypedKeyword,
    UseKeyword,
    UWireKeyword,
    VarKeyword,
    VectoredKeyword,
    VirtualKeyword,
    VoidKeyword,
    WaitKeyword,
    WaitOrderKeyword,
    WAndKeyword,
    WeakKeyword,
    Weak0Keyword,
    Weak1Keyword,
    WhileKeyword,
    WildcardKeyword,
    WireKeyword,
    WithKeyword,
    WithinKeyword,
    WOrKeyword,
    XnorKeyword,
    XorKeyword,

    // predefined system keywords
    UnitSystemName,
    RootSystemName,

    // directives (these get consumed by the preprocessor and don't
    // make it downstream to the parser)
    Directive,
    IncludeFileName,
    MacroUsage,
    MacroQuote,
    MacroEscapedQuote,
    MacroPaste,
    EmptyMacroArgument,
    LineContinuation
};

} // namespace slang

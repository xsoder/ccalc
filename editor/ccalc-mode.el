;;; ccalc-mode.el --- Major mode for ccalc math language -*- lexical-binding: t; -*-

;; Copyright (C) 2025
;; Author: xsoder
;; Keywords: languages, math, ccalc
;; Version: 0.1
;; Package-Requires: ((emacs "25.3"))

;;; Commentary:

;; Major mode for editing and running ccalc math language files.
;; Provides syntax highlighting, indentation, and an inferior REPL mode
;; similar to Haskell mode.
;;
;; Usage:
;;   M-x ccalc-mode         - Enable major mode for .calc files
;;   M-x run-ccalc          - Start interactive REPL
;;   C-c C-l                - Load current file into REPL
;;   C-c C-r                - Send region to REPL
;;   C-c C-c                - Send current definition to REPL
;;   M-x compile            - Compile with error navigation

(require 'comint)
(require 'compile)

;;; ============================================================================
;;; Customization
;;; ============================================================================

(defgroup ccalc nil
  "Major mode for ccalc language."
  :group 'languages
  :prefix "ccalc-")

(defcustom ccalc-program-name "ccalc"
  "Program invoked by the `run-ccalc' command."
  :type 'string
  :group 'ccalc)

(defcustom ccalc-use-colors t
  "Whether to use --color flag when running ccalc."
  :type 'boolean
  :group 'ccalc)

(defcustom ccalc-prompt-regexp "^λ "
  "Regexp to match ccalc REPL prompt."
  :type 'regexp
  :group 'ccalc)

;;; ============================================================================
;;; Syntax Table
;;; ============================================================================

(defvar ccalc-mode-syntax-table
  (let ((st (make-syntax-table)))
    ;; Comments: # to end of line
    (modify-syntax-entry ?# "<" st)
    (modify-syntax-entry ?\n ">" st)
    ;; Strings
    (modify-syntax-entry ?\" "\"" st)
    (modify-syntax-entry ?\' "\"" st)
    ;; Operators
    (modify-syntax-entry ?_ "w" st)
    (modify-syntax-entry ?+ "." st)
    (modify-syntax-entry ?- "." st)
    (modify-syntax-entry ?* "." st)
    (modify-syntax-entry ?/ "." st)
    (modify-syntax-entry ?% "." st)
    (modify-syntax-entry ?< "." st)
    (modify-syntax-entry ?> "." st)
    (modify-syntax-entry ?= "." st)
    (modify-syntax-entry ?! "." st)
    st)
  "Syntax table for `ccalc-mode'.")

;;; ============================================================================
;;; Keywords and Font-lock
;;; ============================================================================

(defconst ccalc-keywords
  '("if" "else" "while" "lambda" "const" "import"
    "return" "break" "continue" "True" "False" "struct" "self"))

(defconst ccalc-builtins
  '("print" "len" "range" "help" "type" "assert" "test" "link" "extern" "match"))

(defconst ccalc-constants
  '("None"))

(defvar ccalc-font-lock-keywords
  `(
    (,(regexp-opt ccalc-keywords 'symbols) . font-lock-keyword-face)
    (,(regexp-opt ccalc-builtins 'symbols) . font-lock-builtin-face)
    (,(regexp-opt ccalc-constants 'symbols) . font-lock-constant-face)
    ("\\<\\([A-Za-z_][A-Za-z0-9_]*\\)\\s-*("
     (1 font-lock-function-name-face))
    ("^\\s-*\\<\\([A-Za-z_][A-Za-z0-9_]*\\)\\s-*="
     (1 font-lock-variable-name-face))
    ("\\<[0-9]+\\(\\.[0-9]+\\)?\\>" . font-lock-constant-face)
    ("lambda\\s-+\\([A-Za-z_][A-Za-z0-9_]*\\)"
     (1 font-lock-variable-name-face))
    )
  "Syntax highlighting for `ccalc-mode'.")

;;; ============================================================================
;;; Indentation
;;; ============================================================================

(defun ccalc-indent-line ()
  "Indent current line for ccalc."
  (interactive)
  (let ((indent-col 0)
        (pos (- (point-max) (point))))
    (save-excursion
      (beginning-of-line)
      ;; De-indent else
      (when (looking-at "^[ \t]*else\\b")
        (setq indent-col (- indent-col tab-width)))
      (when (looking-at "^[ \t]*}")
        (setq indent-col (- indent-col tab-width)))
      ;; Look backwards for block openers
      (when (and (not (bobp))
                 (save-excursion
                   (forward-line -1)
                   (or (looking-at ".*{\\s-*$")
                       (looking-at ".*:\\s-*$"))))
        (forward-line -1)
        (setq indent-col (+ (current-indentation) tab-width))))
    (indent-line-to (max indent-col 0))
    (when (> (- (point-max) pos) (point))
      (goto-char (- (point-max) pos)))))

;;; ============================================================================
;;; Compilation Mode Integration
;;; ============================================================================

(eval-after-load 'compile
  '(progn
     (add-to-list 'compilation-error-regexp-alist 'ccalc)
     (add-to-list 'compilation-error-regexp-alist-alist
                  '(ccalc
                    "^\\([^:]+\\):\\([0-9]+\\):\\([0-9]+\\): \\(error\\|warning\\): \\(.*\\)$"
                    1 2 3 (4 . nil) 5))))

(defun ccalc-compile ()
  "Compile the current ccalc file."
  (interactive)
  (let* ((file (buffer-file-name))
         (command (concat ccalc-program-name " " 
                         (when ccalc-use-colors "--color ")
                         (shell-quote-argument file))))
    (compile command)))

;;; ============================================================================
;;; Inferior CCal Mode (REPL)
;;; ============================================================================

(defvar inferior-ccalc-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map "\C-c\C-l" 'ccalc-load-file)
    map)
  "Keymap for inferior ccalc mode.")

(define-derived-mode inferior-ccalc-mode comint-mode "Inferior CCal"
  "Major mode for interacting with an inferior ccalc process.

\\{inferior-ccalc-mode-map}"
  (setq comint-prompt-regexp ccalc-prompt-regexp)
  (setq comint-use-prompt-regexp t)
  (setq comint-process-echoes nil)
  (setq comint-prompt-read-only t))

(defvar ccalc-buffer nil
  "The current ccalc process buffer.")

;;;###autoload
(defun run-ccalc ()
  "Run an inferior instance of ccalc."
  (interactive)
  (let* ((ccalc-program (if ccalc-use-colors
                            (concat ccalc-program-name " --color")
                          ccalc-program-name))
         (buffer (comint-check-proc "CCal")))
    (pop-to-buffer-same-window
     (if (or buffer (not (derived-mode-p 'inferior-ccalc-mode))
             (comint-check-proc (current-buffer)))
         (get-buffer-create (or buffer "*CCal*"))
       (current-buffer)))
    ;; Create the process if there is none
    (unless buffer
      (make-comint-in-buffer "CCal" buffer
                            ccalc-program-name nil
                            (when ccalc-use-colors "--color"))
      (inferior-ccalc-mode))
    (setq ccalc-buffer (current-buffer))
    buffer))

(defun ccalc-load-file (file)
  "Load a ccalc FILE into the inferior ccalc process."
  (interactive (list (or (buffer-file-name)
                        (read-file-name "Load ccalc file: " nil nil t))))
  (comint-check-source file)
  (let ((buffer (or ccalc-buffer (run-ccalc))))
    (comint-send-string buffer (format "import \"%s\"\n" file))
    (pop-to-buffer buffer)))

(defun ccalc-send-region (start end)
  "Send the current region to the inferior ccalc process."
  (interactive "r")
  (let ((buffer (or ccalc-buffer (run-ccalc))))
    (comint-send-region buffer start end)
    (comint-send-string buffer "\n")))

(defun ccalc-send-definition ()
  "Send the current definition to the inferior ccalc process."
  (interactive)
  (save-excursion
    (end-of-line)
    (let ((end (point)))
      (beginning-of-line)
      ;; Skip back to start of definition
      (while (and (not (bobp))
                  (save-excursion
                    (forward-line -1)
                    (or (looking-at "^[ \t]*$")
                        (looking-at "^[ \t]+"))))
        (forward-line -1))
      (ccalc-send-region (point) end))))

(defun ccalc-send-buffer ()
  "Send the entire buffer to the inferior ccalc process."
  (interactive)
  (ccalc-send-region (point-min) (point-max)))

(defun ccalc-switch-to-repl ()
  "Switch to the ccalc REPL buffer."
  (interactive)
  (if (and ccalc-buffer (buffer-live-p ccalc-buffer))
      (pop-to-buffer ccalc-buffer)
    (run-ccalc)))

;;; ============================================================================
;;; Keymap
;;; ============================================================================

(defvar ccalc-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map "\C-c\C-l" 'ccalc-load-file)
    (define-key map "\C-c\C-r" 'ccalc-send-region)
    (define-key map "\C-c\C-c" 'ccalc-send-definition)
    (define-key map "\C-c\C-b" 'ccalc-send-buffer)
    (define-key map "\C-c\C-z" 'ccalc-switch-to-repl)
    ;; Compilation
    (define-key map "\C-c\C-k" 'ccalc-compile)
    map)
  "Keymap for `ccalc-mode'.")

;;; ============================================================================
;;; Mode Definition
;;; ============================================================================

;;;###autoload
(define-derived-mode ccalc-mode prog-mode "CCal"
  "Major mode for editing ccalc math language files.

Key bindings:
\\{ccalc-mode-map}"
  :syntax-table ccalc-mode-syntax-table
  (setq-local font-lock-defaults '(ccalc-font-lock-keywords))
  (setq-local comment-start "# ")
  (setq-local comment-end "")
  (setq-local comment-start-skip "#+\\s-*")
  (setq-local indent-line-function #'ccalc-indent-line)
  (setq-local tab-width 4)
  (setq-local electric-indent-chars
              (append '(?\{ ?\} ?\:) electric-indent-chars))
  (setq-local imenu-generic-expression
              '(("Functions" "^\\s-*\\([A-Za-z_][A-Za-z0-9_]*\\)\\s-*(" 1)))
  (setq-local which-func-functions
              '(ccalc-which-function)))

(defun ccalc-which-function ()
  "Return the current function name for mode-line display."
  (save-excursion
    (beginning-of-line)
    (when (re-search-backward "^\\([A-Za-z_][A-Za-z0-9_]*\\)\\s-*(" nil t)
      (match-string-no-properties 1))))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.calc\\'" . ccalc-mode))

;;; ============================================================================
;;; Menu
;;; ============================================================================

(easy-menu-define ccalc-mode-menu ccalc-mode-map
  "Menu for CCal mode."
  '("CCal"
    ["Start REPL" run-ccalc t]
    ["Switch to REPL" ccalc-switch-to-repl t]
    "---"
    ["Load File" ccalc-load-file t]
    ["Send Region" ccalc-send-region (use-region-p)]
    ["Send Definition" ccalc-send-definition t]
    ["Send Buffer" ccalc-send-buffer t]
    "---"
    ["Compile File" ccalc-compile (buffer-file-name)]
    "---"
    ["Indent Line" indent-for-tab-command t]))

(provide 'ccalc-mode)

;;; ccalc-mode.el ends here

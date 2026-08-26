// Repository-owned JavaScript lint policy for workflow automation and tests.
const js = require('@eslint/js');

module.exports = [
    {
        ignores: ['build/**', 'external/**'],
    },
    js.configs.recommended,
    {
        files: ['**/*.js'],
        languageOptions: {
            ecmaVersion: 'latest',
            sourceType: 'commonjs',
            globals: {
                Buffer: 'readonly',
                __dirname: 'readonly',
                console: 'readonly',
                exports: 'writable',
                module: 'writable',
                process: 'readonly',
                require: 'readonly',
            },
        },
        rules: {
            curly: 'error',
            eqeqeq: 'error',
            'no-throw-literal': 'error',
            'no-var': 'error',
            'prefer-const': 'error',
        },
    },
];
